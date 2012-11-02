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
#include "diagnostic.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "toplev.h"

/*-----------------------------------------------------------------------------
 *  Each plugin MUST define this global int to assert compatibility with GPL; 
 *  else the compiler throws a fatal error 
 *-----------------------------------------------------------------------------*/
int plugin_is_GPL_compatible;  

typedef struct datastore {
	tree var;
	tree rhs;
	int val;
	struct datastore *next;
} datastore;

datastore *store;

/*function declarations*/
static unsigned int const_propagation (void);
static datastore * create_store(tree, int, tree);
static void insert_entry(tree, int, tree);
static void delete_entry(tree);
static datastore * get_cst_var(tree);
static void print_store(void);

/*-----------------------------------------------------------------------------
 *  Structure of the pass we want to insert, identical to a regular ipa pass
 *-----------------------------------------------------------------------------*/
struct gimple_opt_pass pass_plugin = {
  {
    GIMPLE_PASS,
    "const-prop",           	        /*  name */
    NULL,                         	/*  gate */
    const_propagation,		        /*  execute */
    NULL,                         	/*  sub */
    NULL,                         	/*  next */
    0,                            	/*  static pass number */
    TV_INTEGRATION,                	/*  tv_id */
    0,                            	/*  properties required */
    0,                            	/*  properties provided */
    0,                            	/*  properties destroyed */
    0,                            	/*  todo_flags start */
    0                             	/*  todo_flags end */
  }
};


/*-----------------------------------------------------------------------------
 *  This structure provides the information about inserting the pass in the
 *  pass manager. 
 *-----------------------------------------------------------------------------*/
struct register_pass_info pass_info = {
  &(pass_plugin.pass),            /* Address of new pass, here, the 'struct
                                     opt_pass' field of 'gimple_opt_pass'
                                     defined above */
  "cfg",                          /* Name of the reference pass for hooking up
                                     the new pass.   ??? */
  0,                              /* Insert the pass at the specified instance
                                     number of the reference pass. Do it for
                                     every instance if it is 0. */
  PASS_POS_INSERT_AFTER           /* how to insert the new pass: before,
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
/* Constant Propagation: propagate value defined in a statement of the form
 * x = c to entire program. Following conditions need to be satisfied:
 *   1. There should be a statement in which variable x is used.
 *   2. All definitions of x reaching this statement should have the same
 *      value.
 * This code considers only the simpler linear code case.
 */
static unsigned int const_propagation (void)
{
        basic_block bb;
	gimple_stmt_iterator gsi;
	datastore * const_var;

        if (!dump_file)
        	return;

        /* Iterating over each basic block of a function */
        FOR_EACH_BB_FN (bb, cfun)
        {
                /*Iterating over each gimple statement in a basic block*/
                for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
		{
			if ( is_gimple_assign(gsi_stmt(gsi)) ) {
				tree lhs = gimple_assign_lhs(gsi_stmt(gsi));
				tree rhs1 = gimple_assign_rhs1(gsi_stmt(gsi));
				tree rhs2 = gimple_assign_rhs2(gsi_stmt(gsi));
				if (DECL_ARTIFICIAL(lhs))
					continue;	/* skip autogenerated variables */

				/* Get the code of expression computed on RHS */
				enum tree_code code = gimple_assign_rhs_code(gsi_stmt(gsi));
				
				switch (get_gimple_rhs_class (code)) {
					/* Expression containing unary operator */
					case GIMPLE_UNARY_RHS:
						fprintf(stdout, "UNARY\n");
						break;
					
					/* Expression containing binary operator */
					case GIMPLE_BINARY_RHS:
						if (TREE_CODE(rhs1) == VAR_DECL) {
							if ((const_var = get_cst_var(rhs1))) {
								gimple new_stmt = gimple_build_assign_with_ops (code, lhs, const_var->rhs, rhs2);
								gsi_replace(&gsi, new_stmt, false);
								//insert_entry(lhs, const_var->val, const_var->rhs);
							}
							else
								delete_entry(lhs);
						}
						/* need to calculate rhs1 again since gsi might get replaced above */
						rhs1 = gimple_assign_rhs1(gsi_stmt(gsi));
						if (TREE_CODE(rhs2) == VAR_DECL) {
							if ((const_var = get_cst_var(rhs2))) {
								gimple new_stmt = gimple_build_assign_with_ops (code, lhs, rhs1, const_var->rhs);
								gsi_replace(&gsi, new_stmt, false);
								//insert_entry(lhs, const_var->val, const_var->rhs);
							}
							else
								delete_entry(lhs);
						}
					break;

					case GIMPLE_SINGLE_RHS:
						if (TREE_CODE(rhs1) == INTEGER_CST) {
							int val = ((TREE_INT_CST_HIGH (rhs1) << HOST_BITS_PER_WIDE_INT) + TREE_INT_CST_LOW (rhs1));
							insert_entry(lhs, val, rhs1);
						}
						else if (TREE_CODE(rhs1) == VAR_DECL) {
							if ((const_var = get_cst_var(rhs1))) {
								gimple new_stmt = gimple_build_assign (lhs, const_var->rhs);
								gsi_replace(&gsi, new_stmt, false);
								insert_entry(lhs, const_var->val, const_var->rhs);
							}
							else
								delete_entry(lhs);
						}
					break;
				}
			}
		}
		print_store();
        }

        /* Display transformed code */;
        FOR_EACH_BB_FN (bb, cfun)
        {
                /*Iterating over each gimple statement in a basic block*/
                for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next (&gsi))
                        print_gimple_stmt (dump_file, gsi_stmt (gsi), 0, 0);
        }

	return 0;
}

static datastore * create_store(tree var, int val, tree rhs)
{
	datastore *temp = (datastore *) ggc_alloc_cleared_atomic (sizeof (datastore));
	temp->var = var;
	temp->val = val;
	temp->rhs = rhs;
	temp->next = NULL;

	return temp;
}

static void insert_entry(tree var, int val, tree rhs)
{
	datastore *temp;
	if (!store)
		store = create_store(var, val, rhs);
	else {
		for (temp = store; temp->next; temp = temp->next) {
			if (temp->var == var) {
				temp->val = val;
				temp->rhs = rhs;
				return;
			}
		}
		if (temp->var == var) {
			temp->val = val;
			temp->rhs = rhs;
			return;
		}
		temp->next = create_store(var, val, rhs);
	}
}

static void delete_entry(tree var)
{
	if (!store)
		return;
	datastore *temp1, *temp2;

	if (store && store->var == var) {
		store = store->next;
		return;
	}
	for (temp1 = store, temp2 = store->next; temp2; temp1 = temp2, temp2 = temp2->next) {
		if (temp2->var == var) {
			temp1->next = temp2->next;
			return;
		}
	}

}

static datastore * get_cst_var(tree var)
{
	datastore *temp;
	for (temp = store; temp; temp = temp->next) {
		if (temp->var == var)
			return temp;
	}
	return NULL;
}

static void print_store(void)
{
	datastore * temp;
	fprintf(dump_file, "\nConstants dump:\n==============\n");
	for (temp = store; temp; temp = temp->next) {
		fprintf(dump_file, "%s:%d, ", get_name(temp->var), temp->val);
	}
	fprintf(dump_file, "\n\n");
}
