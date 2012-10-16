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

/*function declarations*/
static unsigned int const_propagation (void);

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
static unsigned int const_propagation (void)
{
        basic_block bb;
	gimple_stmt_iterator gsi;

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
				tree rhs = gimple_assign_rhs1(gsi_stmt(gsi));

				if (!DECL_ARTIFICIAL(lhs) && TREE_CODE(rhs) == INTEGER_CST)
					fprintf(dump_file, "Var: %s\t Val: %d\n", get_name(lhs)
							, ((TREE_INT_CST_HIGH (rhs) << HOST_BITS_PER_WIDE_INT) + TREE_INT_CST_LOW (rhs)));
					//print_gimple_stmt (dump_file, gsi_stmt(gsi), 0, 0);
			}
		}
        }

	return 0;
}
