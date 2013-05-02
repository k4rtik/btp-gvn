/*-----------------------------------------------------------------------------
 *  "gcc-plugin.h" must be the FIRST file to be included 
 *-----------------------------------------------------------------------------*/
#include "gcc-plugin.h"
#include "config.h"
#include <stdio.h>
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tm_p.h"
#include "tree.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "toplev.h"

#define POOLMAX 30

/*-----------------------------------------------------------------------------
 *  Each plugin MUST define this global int to assert compatibility with GPL; 
 *  else the compiler throws a fatal error 
 *-----------------------------------------------------------------------------*/
int plugin_is_GPL_compatible;  

/* A structure for storing constant values */
//typedef struct const_val_container
//{
//        tree var, rhs1;
//        int val;
//        struct const_val_container * next;
//}const_val_container;
//
//const_val_container * cp;

struct pointer_map_t *map;

struct node
{
	tree *exp;
	struct node *next;
};

/* Data structure holding expression pools at a node - first element is vn */
struct exp_poolset
{
	struct node in[POOLMAX];       /* EIN */
	struct node out[POOLMAX];      /* EOUT */
	struct node out_prev[POOLMAX]; /* EOUT in the previous iteration */

};

gimple_stmt_iterator gsi;

/*function declarations*/
static unsigned int do_gvn (void);
static void initialize_exp_poolset(gimple stmt);
static bool change_in_exp_pools();
static void set_in_pool(gimple_stmt_iterator gsi, basic_block bb);
static void do_confluence(gimple_stmt_iterator gsi, basic_block bb);
static void set_out_pool(gimple stmt);
static void transfer(gimple stmt);
//static unsigned int copy_propagation (void);
//static void analyze_gimple_statement (gimple);
//static const_val_container * allocate_container (tree, int, tree);
//static void insert_in_const_container (tree, int, tree);
//static const_val_container * is_constant (tree);
//static void delete_from_const_container (tree);

/*-----------------------------------------------------------------------------
 *  Structure of the pass we want to insert, identical to a regular ipa pass
 *-----------------------------------------------------------------------------*/
struct gimple_opt_pass pass_plugin = {
  {
        GIMPLE_PASS,
        "sagvn_fre",                  /*  name */
        NULL,                         /*  gate */
        do_gvn,                       /*  execute */
        NULL,                         /*  sub */
        NULL,                         /*  next */
        0,                            /*  static pass number */
        TV_INTEGRATION,               /*  tv_id */
        0,                            /*  properties required */
        0,                            /*  properties provided */
        0,                            /*  properties destroyed */
        0,                            /*  todo_flags start */
        0                             /*  todo_flags end */
  }
};


/*-----------------------------------------------------------------------------
 *  This structure provides the information about inserting the pass in the
 *  pass manager. 
 *-----------------------------------------------------------------------------*/
struct register_pass_info pass_info = {
&(pass_plugin.pass),                         /* Address of new pass, here, the 'struct
                                             opt_pass' field of 'gimple_opt_pass'
                                             defined above */
        "cfg",                               /* Name of the reference pass for hooking up
                                             the new pass.   ??? */
        0,                                   /* Insert the pass at the specified instance
                                             number of the reference pass. Do it for
                                             every instance if it is 0. */
        PASS_POS_INSERT_AFTER                /* how to insert the new pass: before,
                                             after, or replace. Here we are inserting
                                             a pass names 'plug' after the pass named
                                             'pta' */
};

/*-----------------------------------------------------------------------------
 *  plugin_init is the first function to be called after the plugin is loaded
 *-----------------------------------------------------------------------------*/
int
plugin_init(struct plugin_name_args *plugin_info,
    struct plugin_gcc_version *version)
{

        /*-----------------------------------------------------------------------------
        * Plugins are activiated using this callback 
        *-----------------------------------------------------------------------------*/
        register_callback (
        plugin_info->base_name,     /* char *name: Plugin name, could be any
                                     name. plugin_info->base_name gives this
                                     filename */
        PLUGIN_PASS_MANAGER_SETUP,  /* int event: The event code. Here, setting
                                     up a new pass */
        NULL,                       /* The function that handles event */
        &pass_info);                /* plugin specific data */

        return 0;
}

/* Execute function */
static unsigned int do_gvn (void)
{
	basic_block bb;
	map = pointer_map_create();

	if (!dump_file)
		return;

	// TODO EOUT_entry = PHI
	FOR_EACH_BB_FN (bb, cfun)
	{
		for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next(&gsi))
			initialize_exp_poolset (gsi_stmt(gsi)); /* EOUTn = T */
	}

	while ( change_in_exp_pools() )
	{
		FOR_EACH_BB_FN (bb, cfun)
		{
			for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next(&gsi))
			{
				set_in_pool(gsi, bb); /* EINn = ^ EOUTp where p in pred(n) */
				set_out_pool(gsi_stmt(gsi)); /* EOUTn = fn(EINn) */
			}
		}
	}
}

static void initialize_exp_poolset(gimple stmt)
{
	int i;
	struct exp_poolset *poolset = ggc_alloc_cleared_atomic(sizeof(struct exp_poolset));
	if (poolset)
		*pointer_map_insert(map, stmt) = (void *) poolset;
	else
		fprintf(stdout, "Initializing memory failed!\n");

	// assign an arbitrary value or null to all pools.
	/* */
	tree *T = NULL;
	fprintf(stdout, "Initializing with T\n");
	//poolset->out = (tree **) ggc_alloc_cleared_atomic(POOLMAX*sizeof(tree*));
	for (i=0; i<POOLMAX; i++) {
		(poolset->out)[i].exp = T;
	}
	// */
}

static bool change_in_exp_pools()
{
	// TODO
	// iterate over all pool_out and pool_out_prev 
	// return false only if they are all the same
	static int i = 0;
	if (i++ <= 10)
		return true;
	else return false;
}

static void set_in_pool(gimple_stmt_iterator gsi, basic_block bb)
{
	gimple_stmt_iterator gsiprev = gsi;
	int i;
	// TODO
	// if not block_start do a simple copy from eout(p) to ein(n)
	// do confluence otherwise
	if (gsi_stmt(gsi_start_bb(bb)) == gsi_stmt(gsi))
		do_confluence(gsi, bb);
	else {
		gsi_prev(&gsiprev);
		for (i=0;i<POOLMAX;i++) {
			((struct exp_poolset *) *pointer_map_contains(map, gsi_stmt(gsi)))->in[i].exp = ((struct exp_poolset*) *pointer_map_contains(map, gsi_stmt(gsiprev)))->out[i].exp;
		}
	}
}

static void do_confluence(gimple_stmt_iterator gsi, basic_block bb)
{
	fprintf(stdout, "Reached confluence.\n");
	// TODO
}

static void set_out_pool(gimple stmt)
{
	// call transfer function
	transfer(stmt);
}

static void transfer(gimple stmt)
{
	struct exp_poolset* poolset = *pointer_map_contains(map, stmt);
	tree *temp_pool[POOLMAX];
	int lclass=-1, rclass=-1;
	int i;
	for (i=0; i<POOLMAX; i++)
		temp_pool[i] = (poolset->in)[i].exp;

	if (is_gimple_assign(stmt)) { // x = e
		tree x = gimple_assign_lhs(stmt);
		if ( (lclass = find(x, temp_pool)) > -1) {
			remove_from_class(x, lclass, temp_pool);
			delete_singletons(temp_pool);
		}
		tree e_ve = value_exp_rhs(stmt);
		if ( (rclass = find(e_ve, temp_pool)) > -1 ) {
			add_to_class(x, rclass, temp_pool);
		}
		else {
			create_new_class(temp_pool, x, e_ve); // create a new value number as well
		}
	}
}

//static unsigned int copy_propagation (void)
//{
//        basic_block bb;
//
//        if (!dump_file)
//        return;
//
//        /* Iterating over each basic block of a function */
//        FOR_EACH_BB_FN (bb, cfun)
//        {
//                /*Iterating over each gimple statement in a basic block*/
//                for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
//                analyze_gimple_statement (gsi_stmt (gsi));
//        }
//
//        /* Display transformed code */;
//        FOR_EACH_BB_FN (bb, cfun)
//        {
//                /*Iterating over each gimple statement in a basic block*/
//                for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
//                        print_gimple_stmt (dump_file, gsi_stmt (gsi), 0, 0);
//        }
//
//        return 0;
//}

/* A function for analyzing GIMPLE statements for Copy propagation  */
//static void analyze_gimple_statement (gimple stmt)
//{
//        const_val_container * get_const_var;
//
//        if (gimple_code (stmt) == GIMPLE_ASSIGN)
//        { 
//                /* Extract LSH and RHS operands of an assignment */      
//                tree lhs = gimple_assign_lhs (stmt);
//                tree rhs1 = gimple_assign_rhs1 (stmt);
//
//                if (!DECL_ARTIFICIAL (lhs)) /* Skip temporary variables */
//                {
//                        if (TREE_CODE (rhs1) == INTEGER_CST)
//                        {
//                                /* extract the constant value */
//                                int val = ((TREE_INT_CST_HIGH (rhs1) << HOST_BITS_PER_WIDE_INT) + TREE_INT_CST_LOW (rhs1));
//        
//                                /* A variable is constant, insert it in constant value container */
//                                if (!DECL_ARTIFICIAL (lhs))
//                                        insert_in_const_container (lhs, val, rhs1);
//                        }
//                        else if (TREE_CODE (rhs1) == VAR_DECL)
//                        {
//                                /* Check if RHS operand variable has constant value; 
//                                   if yes, mark LHS operand also as a constant, modify GIMPLE statement. */
//                                if ((get_const_var = is_constant (rhs1)))
//                                {
//                                        gimple new_stmt = gimple_build_assign (lhs, get_const_var->rhs1);        
//                                        gsi_replace (&gsi, new_stmt, false);
//                                        insert_in_const_container (lhs, get_const_var->val, get_const_var->rhs1);
//                                }
//                                else
//                                {
//                                        /* If RHS operand variable does not have constant value; 
//                                           delete it from constant value container (if present). */
//                                        delete_from_const_container (lhs);
//                                }
//                        }
//                }
//        }
//}

//static void 
//insert_in_const_container (tree var, int val, tree rhs1)
//{
//        const_val_container * temp;
//
//        if (!cp)
//                cp = allocate_container (var, val, rhs1);  
//        else 
//        {
//                for (temp = cp; temp->next; temp = temp->next)
//                {
//                        if (temp->var == var)
//                        {
//                                temp->val = val;
//                                temp->rhs1 = rhs1;
//                                return;
//                        }
//                }
//                if (temp->var == var)
//                {
//                        temp->val = val;
//                        temp->rhs1 = rhs1;
//                        return;
//                }
//                temp->next = allocate_container (var, val, rhs1);
//        }
//}

//static const_val_container *
//allocate_container (tree var, int val, tree rhs1)
//{
//        const_val_container * temp = (const_val_container *) ggc_alloc_cleared_atomic (sizeof (const_val_container));
//        temp->var = var;
//        temp->val = val;
//        temp->rhs1 = rhs1;
//        temp->next = NULL;
//        return temp;
//}

//static void 
//delete_from_const_container (tree var)
//{
//        const_val_container * temp, *temp1;
//
//        if (cp && cp->var == var)
//        {
//                temp = cp;
//                cp = cp->next;
//                return;
//        }
//        for (temp = temp1 = cp; temp1; temp1 = temp, temp = temp->next)
//        {
//                if (temp->var == var)
//                {
//                        temp1->next = temp->next;
//                        break;
//                }
//        }
//}

//static const_val_container *
//is_constant (tree var)
//{
//        const_val_container * temp;
//        for (temp = cp; temp; temp = temp->next)
//        {
//                if (temp->var == var)
//                return temp;
//        }
//        return NULL; 
//}
