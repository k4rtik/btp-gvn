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

#define POOLMAX 9

/*-----------------------------------------------------------------------------
 *  Each plugin MUST define this global int to assert compatibility with GPL; 
 *  else the compiler throws a fatal error 
 *-----------------------------------------------------------------------------*/
int plugin_is_GPL_compatible;  

struct pointer_map_t *map;

struct node
{
	tree exp;
	struct node *next;
};

/* Data structure holding expression pools at a node - first element is vn */
struct exp_poolset
{
	struct node *in[POOLMAX];       /* EIN */
	struct node *out[POOLMAX];      /* EOUT */
	struct node *out_prev[POOLMAX]; /* EOUT in the previous iteration */
};

gimple_stmt_iterator gsi;
tree T;

/*function declarations*/
static unsigned int do_gvn (void);
static void initialize_exp_poolset(gimple stmt);
static void process_out_prev_pool(gimple stmt);
static bool change_in_exp_pools();
static void set_in_pool(gimple_stmt_iterator gsi, basic_block bb);
static struct node *clone_list(struct node *list);
static void do_confluence(gimple_stmt_iterator gsi, basic_block bb);
static void set_out_pool(gimple stmt);
static void transfer(gimple stmt);
static int find_vn_class(tree t, struct node *pool[]);
static int find_ve_class(enum tree_code code, tree lhs, tree rhs, struct node *pool[]);
static int remove_ve_from_pool(tree t, struct node* pool[]);
static void remove_from_class(tree t, int class, struct node *pool[]);
static void delete_singletons(struct node *pool[]);
static tree value_exp_rhs(gimple stmt, struct node *pool[]);
static void add_to_class(tree t, int class, struct node *pool[]);
static void create_new_class(struct node *pool[], tree t, tree e_ve);
static void create_new_class1(struct node *pool[], tree t);
static void print_poolset(struct exp_poolset *poolset);
static void print_pool(const char name[], struct node *pool[]);

/*-----------------------------------------------------------------------------
 *  Structure of the pass we want to insert, identical to a regular ipa pass
 *-----------------------------------------------------------------------------*/
struct gimple_opt_pass pass_plugin = {
  {
        GIMPLE_PASS,
        "sagvn",                      /*  name */
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
	T = create_tmp_var(integer_type_node, "t");

	if (!dump_file)
		return;

	FOR_EACH_BB_FN (bb, cfun)
	{
		for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next(&gsi))
			initialize_exp_poolset (gsi_stmt(gsi)); /* EOUTn = T */
	}

	do {
		FOR_EACH_BB_FN (bb, cfun)
		{
			for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next(&gsi))
			{
				process_out_prev_pool(gsi_stmt(gsi));
				set_in_pool(gsi, bb); /* EINn = ^ EOUTp where p in pred(n) */
				//fprintf(dump_file, "back from in\n");
				set_out_pool(gsi_stmt(gsi)); /* EOUTn = fn(EINn) */
				/*print_poolset((struct exp_poolset *) 
						*pointer_map_contains(map, gsi_stmt(gsi)));*/
			}
		}
	} while ( change_in_exp_pools() );

}

static void initialize_exp_poolset(gimple stmt)
{
	int i;
	struct exp_poolset *poolset = ggc_alloc_cleared_atomic(sizeof(struct exp_poolset));
	if (poolset)
		*pointer_map_insert(map, stmt) = (void *) poolset;
	else
		fprintf(dump_file, "Initializing memory failed!\n");
	/* */
	// assign an arbitrary value or null to all pools.
	for (i=0; i<POOLMAX; i++) {
		(poolset->out)[i] = ggc_alloc_cleared_atomic(sizeof(struct node));
		(poolset->out)[i]->exp = T;
		(poolset->out_prev)[i] = ggc_alloc_cleared_atomic(sizeof(struct node));
		(poolset->out_prev)[i]->exp = T;
		(poolset->in)[i] = ggc_alloc_cleared_atomic(sizeof(struct node));
	}
	//print_pool("Initial", poolset->out);
	// */
}

static void process_out_prev_pool(gimple stmt)
{
	int i;
	for (i=0;i<POOLMAX;i++)
		((struct exp_poolset *) *pointer_map_contains(map, stmt))->out_prev[i] =
			clone_list(((struct exp_poolset*)
						*pointer_map_contains(map, stmt))->out[i]);
}

static bool change_in_exp_pools()
{
	basic_block bb;
	int i;
	struct node **out, **prev;
	struct node *outtemp, *prevtemp;
	FOR_EACH_BB_FN (bb, cfun)
	{
		for (gsi = gsi_start_bb (bb); !gsi_end_p (gsi); gsi_next(&gsi))
		{
			out = ((struct exp_poolset *)
					*pointer_map_contains(map, gsi_stmt(gsi)))->out;
			prev = ((struct exp_poolset *)
					*pointer_map_contains(map, gsi_stmt(gsi)))->out_prev;
			fprintf(dump_file, "\nReached change_detector\n");
			//print_pool("prev", prev);
			//print_pool("out", out);

			if (!out || !prev)
				return  true;
			for (i=0; i<POOLMAX; i++) {
				if (!(prev[i]->next) && out[i]->next)
					return true;
				for (outtemp=out[i], prevtemp=prev[i];
						outtemp->next && prevtemp->next;
						outtemp=outtemp->next, prevtemp=prevtemp->next) {
					if ((outtemp->next->exp != prevtemp->next->exp)
							&& !(get_gimple_rhs_class(TREE_CODE(prevtemp->next->exp))
								== GIMPLE_BINARY_RHS))
						return true;
				}
			}
		}
	}
	fprintf(dump_file, "\nAnd we terminate finally!\n");
	return false;
}

static void set_in_pool(gimple_stmt_iterator gsi, basic_block bb)
{
	gimple_stmt_iterator gsiprev = gsi;
	int i;
	// if not block_start do a simple copy from eout(p) to ein(n)
	// do confluence otherwise
	if (gsi_stmt(gsi_start_bb(bb)) == gsi_stmt(gsi))
		do_confluence(gsi, bb);
	else {
		//fprintf(dump_file, "non-block-start gimple reached.\n");
		gsi_prev(&gsiprev);
		for (i=0;i<POOLMAX;i++) {
			((struct exp_poolset *) *pointer_map_contains(map, gsi_stmt(gsi)))->in[i] =
				clone_list(((struct exp_poolset*) *pointer_map_contains(map,
								gsi_stmt(gsiprev)))->out[i]);
		}
	}
}

static struct node *clone_list(struct node *list)
{
	if (list == NULL) {
		return NULL;
	}
	struct node *copy = ggc_alloc_cleared_atomic(sizeof(struct node));
	copy->exp = list->exp;
	copy->next = clone_list(list->next);
	return copy;
}

static void do_confluence(gimple_stmt_iterator gsi, basic_block bb)
{
	fprintf(dump_file, "Reached confluence.\n");
	// TODO
}

static void set_out_pool(gimple stmt)
{
	transfer(stmt);
}

static void transfer(gimple stmt)
{
	struct exp_poolset* poolset = *pointer_map_contains(map, stmt);
	struct node *temp_pool[POOLMAX];
	int lclass=-1, rclass=-1;
	int i;
	for (i=0; i<POOLMAX; i++) // E(t) = EIN(n)
		temp_pool[i] = clone_list((poolset->in)[i]);

	//fprintf(dump_file, "reached tranfer\n");
	print_pool("temp_pool", temp_pool);
	if (is_gimple_assign(stmt)) { // x = e
		tree x = gimple_assign_lhs(stmt);
		if ( (lclass = find_vn_class(x, temp_pool)) > -1) {
			fprintf(dump_file, "\n%s found!\n", get_name(x));
			remove_from_class(x, lclass, temp_pool);
			delete_singletons(temp_pool);
			//fprintf(dump_file, "back from singleton-deletion\n");
		} else
			fprintf(dump_file, "\n%s not found!\n", get_name(x));

		tree e_ve = value_exp_rhs(stmt, temp_pool);
		/**
		if (TREE_CODE(e_ve) == INTEGER_CST)
			fprintf(dump_file, "e_ve = %lu\n",
					((TREE_INT_CST_HIGH (e_ve) << HOST_BITS_PER_WIDE_INT)
					 + TREE_INT_CST_LOW (e_ve)));
		else
			fprintf(dump_file, "e_ve = %s\n", get_name(e_ve));
		// */

		if ( (rclass = find_vn_class(e_ve, temp_pool)) > -1 ) {
			add_to_class(x, rclass, temp_pool);
		}
		else {
			create_new_class(temp_pool, x, e_ve);
			print_pool("temp with new class", temp_pool);
		}
		for (i=0;i<POOLMAX;i++)
			((struct exp_poolset *) *pointer_map_contains(map, gsi_stmt(gsi)))->out[i] =
				clone_list(temp_pool[i]);
	}
	else {
		for (i=0;i<POOLMAX;i++)
			((struct exp_poolset *) *pointer_map_contains(map, gsi_stmt(gsi)))->out[i] =
				clone_list(((struct exp_poolset*) *pointer_map_contains(map,
								gsi_stmt(gsi)))->in[i]);
	}
}

static int find_vn_class(tree t, struct node *pool[])
{
	int i;
	for (i=0; i<POOLMAX; i++) {
		struct node *temp = pool[i];
		for (;temp; temp=temp->next)
			if (temp->exp == t)
				return i;
	}
	return -1;
}

static int find_ve_class(enum tree_code code, tree rhs1, tree rhs2, struct node *pool[])
{
	int i;
	//fprintf(dump_file, "Check for %u\n", code);
	for (i=0; i<POOLMAX; i++) {
		struct node *temp = pool[i];
		for (; temp && temp->exp; temp=temp->next) {
			//fprintf(dump_file, "%u, ", TREE_CODE(temp->exp));
			if (TREE_CODE(temp->exp) == INTEGER_CST
					|| TREE_CODE(temp->exp) == VAR_DECL)
				continue;
			else {
				if (TREE_OPERAND(temp->exp,0) == rhs1
						&& TREE_OPERAND(temp->exp,1) == rhs2
						&& TREE_CODE(temp->exp) == code)
					return i;
			}
		}
	}
	return -1;
}

static int remove_ve_from_pool(tree t, struct node* pool[])
{
	int i, flag = 0;
	for (i=0; i<POOLMAX; i++) {
		struct node *temp = pool[i];
		for (; temp->next; temp=temp->next) {
			if (TREE_CODE(temp->next->exp) == INTEGER_CST
					|| TREE_CODE(temp->next->exp) == VAR_DECL)
				continue;
			else if (TREE_OPERAND(temp->next->exp, 0) == t
					|| TREE_OPERAND(temp->next->exp, 1) == t) {
				temp->next = temp->next->next;
				flag = 1;
				print_pool("Just deleted a ve", pool);
			}
		}
	}
	return flag;
}

static void remove_from_class(tree t, int class, struct node *pool[])
{
	struct node *temp = pool[class];
	if (temp->exp == t) {
		temp->exp = NULL;
	}
	for (;temp->next; temp=temp->next)
		if (temp->next->exp == t) {
			temp->next = temp->next->next;
			print_pool("Just deleted a vn", pool);
			return;
		}
}

static void delete_singletons(struct node *pool[])
{
	int i, flag = 0;
	for (i=0; i<POOLMAX; i++) {
		if (pool[i]->exp && pool[i]->next == NULL) {
			flag = remove_ve_from_pool(pool[i]->exp, pool);
			remove_from_class(pool[i]->exp, i, pool);
		}
	}
	if (flag)
		delete_singletons(pool);
}

static tree value_exp_rhs(gimple stmt, struct node *pool[])
{
	if (!is_gimple_assign(stmt))
		return NULL;
	tree rhs1 = gimple_assign_rhs1(stmt);
	tree rhs2 = gimple_assign_rhs2(stmt);

	int lclass = -1, rclass = -1;
	int veclass = -1;

	tree lvn, rvn;
	enum tree_code code = gimple_assign_rhs_code(stmt);

	switch (get_gimple_rhs_class(code)) {
		case GIMPLE_SINGLE_RHS:
			return rhs1;
		case GIMPLE_BINARY_RHS:
			if ((lclass = find_vn_class(rhs1, pool)) == -1)
				create_new_class1(pool, rhs1);
			lvn = pool[lclass]->exp;
			if ((rclass = find_vn_class(rhs2, pool)) == -1)
				create_new_class1(pool, rhs2);
			rvn = pool[rclass]->exp;
			if ((veclass = find_ve_class(code, lvn, rvn, pool)) > -1)
				return pool[veclass]->exp;
			else
				return build2(code, void_type_node, lvn, rvn);
	}
}

static void add_to_class(tree t, int class, struct node *pool[])
{
	struct node *temp = pool[class];
	struct node *new = ggc_alloc_cleared_atomic(sizeof(struct node));
	new->exp = t;
	new->next = NULL;
	for (;temp->next; temp = temp->next)
		;
	temp->next = new;
}

static void create_new_class(struct node *pool[], tree x, tree e)
{
	int i;
	for (i=0;pool[i]->exp!=NULL; i++)
		;
	struct node *xnode, *enode;
	xnode = ggc_alloc_cleared_atomic(sizeof(struct node));
	enode = ggc_alloc_cleared_atomic(sizeof(struct node));
	tree vn = create_tmp_var(integer_type_node, "v");

	enode->exp = e;
	enode->next = NULL;
	xnode->exp = x;
	xnode->next = enode;
	pool[i]->exp = vn;
	pool[i]->next = xnode;
}

static void create_new_class1(struct node *pool[], tree x)
{
	int i;
	print_pool("temppool-before", pool);
	for (i=0;pool[i]->exp!=NULL; i++)
		;
	struct node *xnode;
	xnode = ggc_alloc_cleared_atomic(sizeof(struct node));
	tree vn = create_tmp_var(integer_type_node, "v");

	xnode->exp = x;
	xnode->next = NULL;
	pool[i]->exp = vn;
	pool[i]->next = xnode;
	print_pool("temppool-after", pool);
}

static void print_poolset(struct exp_poolset *poolset)
{
	print_pool("EIN", poolset->in);
	print_pool("EOUT", poolset->out);
	print_pool("EOUT_PREV", poolset->out_prev);
}

static void print_pool(const char name[], struct node *pool[])
{
	int i;
	struct node *temp;
	fprintf(dump_file, "\n%s(n):\n=============", name);
	for (i=0; i<POOLMAX; i++) {
		fprintf(dump_file, "\nClass %d ", i);
		for (temp = pool[i]; temp && temp->exp; temp=temp->next) {
			switch (TREE_CODE(temp->exp)) {
				case INTEGER_CST:
					fprintf(dump_file, "-> %lu ",
							((TREE_INT_CST_HIGH (temp->exp) << HOST_BITS_PER_WIDE_INT)
							 + TREE_INT_CST_LOW (temp->exp)));
					break;
				case VAR_DECL:
					fprintf(dump_file, "-> %s ", get_name(temp->exp));
					break;
				case PLUS_EXPR:
					fprintf(dump_file, "-> %s + %s ",
							get_name(TREE_OPERAND(temp->exp, 0)),
							get_name(TREE_OPERAND(temp->exp, 1)));
					break;
				case MINUS_EXPR:
					fprintf(dump_file, "-> %s - %s ",
							get_name(TREE_OPERAND(temp->exp, 0)),
							get_name(TREE_OPERAND(temp->exp, 1)));
					break;
				default:
					fprintf(dump_file, "-> UNKNOWN_TYPE ");
			}
		}
	}
	fprintf(dump_file, "\n");
}
