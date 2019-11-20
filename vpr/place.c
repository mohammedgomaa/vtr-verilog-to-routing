#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "util.h"
#include "pr.h"
#include "ext.h"

#define ERROR_TOL .001

void dump_clbs (void);   /* For debugging */

/* [0..num_nets-1]  0 if net never connects to the same block more than  *
 *  once, otherwise it gives the number of duplicate connections.        */

static int *duplicate_pins;    

/* [0..num_nets-1][0..num_unique_blocks-1]  Contains a list of blocks with *
 * no duplicated blocks for ONLY those nets that had duplicates.           */

static int **unique_pin_list;

/* Stores the maximum and expected occupancies, plus the cost, of each   *
 * region in the placement.  Used only by the NONLINEAR_CONG cost        *
 * function.  [0..num_region-1][0..num_region-1].  Place_region_x and    *
 * y give the situation for the x and y directed channels, respectively. */

static struct s_place_region **place_region_x, **place_region_y;

float *place_region_bounds_x, *place_region_bounds_y;


void place_and_route (int operation, float place_cost_exp,
    int place_cost_type, int num_regions, enum pfreq place_freq,
    int place_chan_width, boolean fixed_pins, int bb_factor,
    int initial_cost_type, float initial_pres_fac, float pres_fac_mult,
    float acc_fac_mult, float bend_cost, int block_update_type,
    int max_block_update, int max_immediate_update, char *place_file,
    char *net_file, char *arch_file, char *route_file,
    boolean full_stats) {

/* This routine is called only when the placement cost function is such  *
 * that the circuit must be re-placed each time the track width changes. */

 struct s_trace **best_routing;  /* Saves the best routing found so far. */
 int current, low, high, final, success;
 char msg[BUFSIZE];

 void try_place (int width_fac, float place_cost_exp, 
    int place_cost_type, int num_regions, boolean fixed_pins);
 void read_place (char *place_file, char *net_file, char *arch_file,
    float place_cost_exp, int place_cost_type, int num_regions,
    int chan_width_factor);
 void print_place (char *place_file, char *net_file, char *arch_file);
 void alloc_and_load_unique_pin_list (void);
 void alloc_place_regions (int num_regions);

 int try_route (int width_fac, int initial_cost_type,
    float initial_pres_fac, float pres_fac_mult, float acc_fac_mult,
    float bend_cost, int block_update_type, int max_block_update,
    int max_immediate_update);
 struct s_trace **alloc_route_structs (void);
 void free_route_structs (struct s_trace **best_routing);
 void load_route_bb (int bb_factor);
 void check_connectivity (void);
 void init_draw_coords (float clb_width);
 void save_routing (struct s_trace **best_routing);
 void restore_routing (struct s_trace **best_routing);
 void init_chan (int width_fac);
 void get_serial_num (void);
 void print_route (char *name);
 void routing_stats (boolean full_stats);

/* Allocate the major routing structures. */
 best_routing = alloc_route_structs();

/* Get a list of pins with no duplicates. */
 alloc_and_load_unique_pin_list ();

/* Allocate storage for subregion data, if needed. */
 if (place_cost_type == NONLINEAR_CONG) {
    alloc_place_regions (num_regions);
 }

 if (place_freq == PLACE_NEVER) {
    read_place (place_file, net_file, arch_file, place_cost_exp,
       place_cost_type, num_regions, place_chan_width);
    load_route_bb (bb_factor);   /* Load the route bounding boxes. */
 }

 else if (place_freq == PLACE_ONCE) {
    try_place (place_chan_width, place_cost_exp, place_cost_type,
                num_regions, fixed_pins);
    print_place (place_file, net_file, arch_file);
    load_route_bb (bb_factor);   /* Load the route bounding boxes. */
 }


 fflush (stdout);
 if (operation == PLACE_ONLY) 
    return;

 current = 6;     /* Binary search part */
 low = high = -1;
 final = -1;


 while (final == -1) {
   fflush (stdout);
#ifdef VERBOSE
   printf ("low, high, current %d %d %d\n",low,high,current);
#endif

/* Check if the channel width is huge to avoid overflow.  Assume the *
 * circuit is unroutable with the current router options if we're    *
 * going to overflow.                                                */

    if (current > 1000000000) {
       printf("This circuit appears to be unroutable with the current "
         "router options.\n");
       printf("Aborting routing procedure.\n");
       exit (1);
    }

    if (place_freq == PLACE_ALWAYS) {
       try_place (current, place_cost_exp, place_cost_type, num_regions,
            fixed_pins);
       load_route_bb (bb_factor);
    }
    success = try_route (current, initial_cost_type, initial_pres_fac,
       pres_fac_mult, acc_fac_mult, bend_cost, block_update_type,
       max_block_update, max_immediate_update);

    if (success) {
       high = current;

/* If we're re-placing constantly, the save placement in case it is best. */

       if (place_freq == PLACE_ALWAYS) { 
          print_place (place_file, net_file, arch_file);
       }
       save_routing (best_routing);   /* Save routing in case it is best. */
       if ((high - low) <= 1)
          final = high;
 
       if (low != -1) {
          current = (high+low)/2;
       } 
       else {
          current = high/2;   /* haven't found lower bound yet */
       } 
    }
 
    else {   /* last route not successful */
       low = current;
       if (high != -1) {
          if ((high - low) <= 1)
             final = high;
          current = (high+low)/2;
       } 
       else {
          current = low*2;  /* Haven't found upper bound yet */
       } 
    }
 }
 
 
/* Restore the best placement (if necessary), the best routing, and  *
 * the best channel widths for final drawing and statistics output.  */
 
 init_chan (final);

 if (place_freq == PLACE_ALWAYS) {
    printf("Reading best placement back in.\n");
    read_place (place_file, net_file, arch_file, place_cost_exp,
       place_cost_type, num_regions, final);
 }

 restore_routing (best_routing);
 check_connectivity();
 get_serial_num ();
 
 printf("Best routing used a channel width factor of %d.\n\n",final);
 routing_stats(full_stats);
 
 pic_on_screen = ROUTING;
 init_draw_coords (6.);
 sprintf(msg,"Routing succeeded with a channel width factor of %d.\n",
    final);
 update_screen (MAJOR, msg);
 
 print_route (route_file);
 free_route_structs (best_routing);
 fflush (stdout);
}


void try_place (int width_fac, float place_cost_exp, 
   int place_cost_type, int num_regions, boolean fixed_pins) {

/* Does almost all the work of placing a circuit.  Width_fac gives the   *
 * width of the widest channel.  Place_cost_exp says what exponent the   *
 * width should be taken to when calculating costs.  This allows a       *
 * greater bias for anisotropic architectures.  Place_cost_type          *
 * determines which cost function is used.  num_regions is used only     *
 * the place_cost_type is NONLINEAR_CONG.                                */

 int tot_iter, inner_iter, success_sum, pins_on_block[3];
 int move_lim, i;
 float t, cost, av_cost, success_rat, std_dev, rlim, new_cost;
 float *old_costs, oldt;
 float **old_region_occ_x, **old_region_occ_y;
 char msg[BUFSIZE];

 void init_place(void);
 void init_chan(int cfactor);
 void init_draw_coords (float clb_width);
 float comp_cost (int method, int place_cost_type, int num_regions);
 int try_swap (float t, float *cost, float rlim, int *pins_on_block,
       int place_cost_type, float **old_region_occ_x, 
       float **old_region_occ_y, int num_regions, boolean fixed_pins);

 void check_place (float cost, int place_cost_type, int num_regions);
 float starting_t (float *old_costs, float *cost_ptr, int *pins_on_block,
    int place_cost_type, float **old_region_occ_x,
    float **old_region_occ_y, int num_regions, boolean fixed_pins);
 void update_t (float *t, float std_dev, float rlim, float success_rat); 
 void update_rlim (float *rlim, float success_rat);
 int exit_crit (float t, float std_dev, float cost);
 float get_std_dev (int n, float *data, float av_data);
 void load_for_fast_cost_update (float place_cost_exp);
 void load_place_regions (int num_regions);
 float recompute_cost (int place_cost_type, int num_regions);

 if (place_cost_type == NONLINEAR_CONG) {
    old_region_occ_x = (float **) alloc_matrix (0, num_regions-1,0,
               num_regions-1, sizeof (float));
    old_region_occ_y = (float **) alloc_matrix (0, num_regions-1,0,
               num_regions-1, sizeof (float));
 }
 else {   /* Shouldn't use them; crash hard if I do!   */
    old_region_occ_x = NULL;
    old_region_occ_y = NULL;
 }

 for (i=0;i<num_nets;i++)
    net[i].tempcost = -1.; /* Used to store costs for moves not yet made. *
                            * and to indicate when a net's cost has been  *
                            * recomputed.                                 */
  
 init_chan(width_fac); 

 if (place_cost_type != NONLINEAR_CONG) {
    load_for_fast_cost_update (place_cost_exp);
 }
 else {
    load_place_regions(num_regions);
 }

 init_place();
 pic_on_screen = PLACEMENT;
 init_draw_coords ((float) width_fac);

/* Storing the number of pins on each type of block makes the swap routine *
 * slightly more efficient.                                                */

 pins_on_block[CLB] = clb_size;
 pins_on_block[INPAD] = 1;
 pins_on_block[OUTPAD] = 1;

/* Gets initial cost and loads bounding boxes. */

 cost = comp_cost (NORMAL, place_cost_type, num_regions); 

 move_lim = inner_num*pow(num_blocks,1.3333);
/* Sometimes I want to run the router with a random placement.  Avoid *
 * using 0 moves to stop division by 0 and 0 length vector problems,  *
 * by setting move_lim to 1 (which is still too small to do any       *
 * significant optimization).                                         */

 if (move_lim == 0) 
    move_lim = 1;      

 old_costs = (float *) my_malloc (move_lim*sizeof(float));
 rlim = (float) max (nx, ny);
 t = starting_t (old_costs, &cost, pins_on_block, place_cost_type,
             old_region_occ_x, old_region_occ_y, num_regions, fixed_pins);
 tot_iter = 0;
 printf("Initial placement cost = %g\n\n",cost);
 printf("T\t\tAv. Cost\tAccept. rat.\tStan. Dev.");
 printf("\tRange limit\tTot. Moves\tAlpha\n");
 std_dev = cost;

 sprintf(msg,"Initial Placement.  Cost: %g.  Channel Factor: %d",
   cost, width_fac);
 update_screen(MAJOR,msg);

 while (exit_crit(t, std_dev, cost) == 0) {
    av_cost = 0.;
    success_sum = 0;
    old_costs[0] = cost;

    for (inner_iter=0; inner_iter < move_lim; inner_iter++) {
       if (try_swap(t, &cost, rlim, pins_on_block, place_cost_type,
             old_region_occ_x, old_region_occ_y, num_regions,
             fixed_pins) == 1) {
          old_costs[success_sum] = cost;
          success_sum++;
          av_cost += cost;
       }
#ifdef VERBOSE
       printf("t = %f  cost = %f   move = %d\n",t, cost, inner_iter);
       if (fabs(cost - comp_cost(CHECK, place_cost_type, num_regions))
             > cost * ERROR_TOL) 
          exit(1);
#endif 
    }

/* Lines below prevent too much round-off error from accumulating *
 * in the cost over many iterations.  This round-off can lead to  *
 * error checks failing because the cost is different from what   *
 * you get when you recompute from scratch.                       */
 
    new_cost = recompute_cost (place_cost_type, num_regions);
    if (fabs(new_cost - cost) > cost * ERROR_TOL) {
       printf("Error in try_place:  new_cost = %f, old cost = %f.\n",
           new_cost, cost);
       exit (1);
    }
    cost = new_cost;

    tot_iter += move_lim;
    success_rat = ((float) success_sum)/ move_lim;
    if (success_sum == 0) {
       av_cost = cost;
    }
    else {
       av_cost /= success_sum;
    }
    std_dev = get_std_dev (success_sum, old_costs, av_cost);

    printf("%f\t%f\t%f\t%f\t%f\t%d\t\t",t, av_cost, success_rat,
       std_dev, rlim, tot_iter);
            
    oldt = t;  /* for finding and printing alpha. */
    update_t (&t, std_dev, rlim, success_rat);

    printf("%f\n",t/oldt);

    sprintf(msg,"Cost: %g.  Temperature: %g",cost,t);
    update_screen(MINOR,msg);
    update_rlim (&rlim, success_rat);

#ifdef VERBOSE 
 dump_clbs();
#endif
 }

 t = 0;   /* freeze out */
 av_cost = 0.;
 success_sum = 0;
 for (inner_iter=0; inner_iter < move_lim; inner_iter++) {
    if (try_swap(t, &cost, rlim, pins_on_block, place_cost_type, 
          old_region_occ_x, old_region_occ_y, num_regions,
          fixed_pins) == 1) {
       old_costs[success_sum] = cost;
       success_sum ++;
       av_cost += cost;
    }
#ifdef VERBOSE 
       printf("t = %f  cost = %f   move = %d\n",t, cost, tot_iter);
#endif
 }
 tot_iter += move_lim;
 success_rat = ((float) success_sum) / move_lim;
 if (success_sum == 0) {
    av_cost = cost;
 }
 else {
    av_cost /= success_sum;
 }
 std_dev = get_std_dev (success_sum, old_costs, av_cost);
 printf("%f\t%f\t%f\t%f\t%f\t%d\n",t, av_cost, success_rat,
    std_dev, rlim, tot_iter);

#ifdef VERBOSE 
 dump_clbs();
#endif

 check_place(cost, place_cost_type, num_regions);
 sprintf(msg,"Final Placement.  Cost: %g.  Channel Factor: %d",cost,
    width_fac);
 printf("Final Placement cost: %g\n",cost);
 update_screen(MAJOR,msg);

/* Free memory. */

 free ((void *) old_costs);

 if (place_cost_type == NONLINEAR_CONG) {
    free_matrix ((void *) old_region_occ_x, 0, num_regions-1,0,
          sizeof (float));
    free_matrix ((void *) old_region_occ_y, 0, num_regions-1,0,
          sizeof (float));
 }

}


float get_std_dev (int n, float *data, float av_data) {
 int i;
 float std_dev;
 
 if (n <= 1) {
    return(0.);
 }
 else {
    std_dev = 0;
    for (i=0;i<n;i++)
       std_dev += (data[i] - av_data) * (data[i] - av_data);
    std_dev /= n - 1;
    std_dev = sqrt (std_dev);
    return(std_dev);
 }
}


void update_rlim (float *rlim, float success_rat) {
 /* Update the range limited to keep acceptance prob. near 0.44.  Use *
  * a floating point rlim to allow gradual transitions at low temps.  */

 float upper_lim;

 *rlim = (*rlim) * (1. - 0.44 + success_rat);
 upper_lim = max(nx,ny);
 *rlim = min(*rlim,upper_lim);
 *rlim = max(*rlim,1.);  
/* *rlim = (float) nx; */
}

#define LAMBDA .7 

void update_t (float *t, float std_dev, float rlim, float success_rat) {
/* Update the temperature according to the annealing schedule selected. */

/*  float fac; */

 if (sched_type == USER_SCHED) {
    *t = alpha_t * (*t);
 }

/* Old standard deviation based stuff is below.  This bogs down hopelessly *
 * for big circuits (alu4 and especially bigkey_mod).                      */

/* ------------------------------------ */
/* else if (std_dev == 0.) {  
    *t = 0.;
 }
 else {
    fac = exp (-LAMBDA*(*t)/std_dev);
    fac = max(0.5,fac);   
    *t = (*t) * fac;
 }   */
/* ------------------------------------- */

 else {
    if (success_rat > 0.96) {
       *t = (*t) * 0.5; 
    }
    else if (success_rat > 0.8) {
       *t = (*t) * 0.9;
    }
    else if (success_rat > 0.15 || rlim > 1.) {
       *t = (*t) * 0.95;
    }
    else {
       *t = (*t) * 0.8; 
    }
 }
}


int exit_crit (float t, float std_dev, float cost) {
/* Return 1 when the exit criterion is met.                        */

 if (sched_type == USER_SCHED) {
    if (t < exit_t) {
       return(1);
    }
    else {
       return(0);
    }
 } 
 
 /* Automatic annealing schedule */

/* Old exit crit. */
/* if (std_dev < 0.0001 * cost) {   */

 if (t < 0.005 * cost / num_nets) {
    return(1);
 }
 else {
    return(0);
 }
}


float starting_t (float *old_costs, float *cost_ptr, int *pins_on_block,
    int place_cost_type, float **old_region_occ_x,
    float **old_region_occ_y, int num_regions, boolean fixed_pins) {

/* Finds the starting temperature (hot condition).              */

 int i, num_accepted;
 float std_dev, av;
 int try_swap (float t, float *cost, float rlim, int *pins_on_block,
       int place_cost_type, float **old_region_occ_x, 
       float **old_region_occ_y, int num_regions, boolean fixed_pins);
 float get_std_dev (int n, float *data, float av_data);

 if (sched_type == USER_SCHED) return (init_t);  

/* Try one move per block.  Set t high so essentially all accepted. */
 num_accepted = 0;
 for (i=0;i<num_blocks;i++) {
    if (try_swap(1.e30,cost_ptr,(float) nx, pins_on_block, place_cost_type,
              old_region_occ_x, old_region_occ_y, num_regions, 
              fixed_pins) == 1) {
       old_costs[i] = *cost_ptr;
       num_accepted++; 
    }   
 }   

/* Initial Temp = 20*std_dev. */
 av = 0;
 for (i=0;i<num_accepted;i++) 
    av += old_costs[i];
 av /= num_accepted;
 
 std_dev = get_std_dev(num_accepted,old_costs,av);
 
#ifdef DEBUG
 if (num_accepted != num_blocks) {
    printf("Warning:  Starting t: %d of %d configurations accepted.\n",
        num_accepted,num_blocks);
 }
#endif

#ifdef VERBOSE
    printf("std_dev: %g, average cost: %g, starting temp: %g\n",
        std_dev, av, 20. * std_dev);
#endif

 return (20.*std_dev);
}


int try_swap (float t, float *cost, float rlim, int *pins_on_block, 
   int place_cost_type, float **old_region_occ_x, 
   float **old_region_occ_y, int num_regions, boolean fixed_pins) {

/* Picks some block and moves it to another spot.  If this spot is   *
 * occupied, switch the blocks.  Assess the change in cost function  *
 * and accept or reject the move.  If rejected, return 0.  If        *
 * accepted return 1.  Pass back the new value of the cost function. * 
 * rlim is the range limiter.  pins_on_block gives the number of     *
 * pins on each type of block (improves efficiency).                 */

 int b_from, x_to, y_to, x_from, y_from, b_to; 
 int off_from, k, inet, keep_switch, io_num, num_of_pins;
 int num_nets_affected, bb_index;
 float delta_c, newcost;
 static struct s_bb *bb_coord_new = NULL;
 static struct s_bb *bb_edge_new = NULL;
 static int *nets_to_update = NULL, *net_block_moved = NULL;

 int assess_swap (float delta_c, float t);
 void find_to (int x_from, int y_from, int type, float rlim, int *x_to,
    int *y_to);
 void get_non_updateable_bb (int inet, struct s_bb *bb_coord_new);
 void update_bb (int inet, struct s_bb *bb_coord_new, struct s_bb *bb_edge_new,
    int xold, int yold, int xnew, int ynew);
 int find_affected_nets (int *nets_to_update, int *net_block_moved, 
    int b_from, int b_to, int num_of_pins);
 float net_cost (int inet, struct s_bb *bb_coords);
 float nonlinear_cong_cost (int num_regions);
 void update_region_occ (int inet, struct s_bb*coords, int add_or_sub,
        int num_regions);
 void save_region_occ (float **old_region_occ_x, 
        float **old_region_occ_y, int num_regions);
 void restore_region_occ (float **old_region_occ_x,
        float **old_region_occ_y, int num_regions);

/* Allocate the local bb_coordinate storage, etc. only once. */

 if (bb_coord_new == NULL) {
    bb_coord_new = (struct s_bb *) my_malloc (2 * clb_size * 
          sizeof (struct s_bb));
    bb_edge_new = (struct s_bb *) my_malloc (2 * clb_size *
          sizeof (struct s_bb));
    nets_to_update = (int *) my_malloc (2 * clb_size * sizeof (int));
    net_block_moved = (int *) my_malloc (2 * clb_size * sizeof (int));
 }

    
 b_from = my_irand(num_blocks - 1);

/* If the pins are fixed we never move them from their initial    *
 * random locations.  The code below could be made more efficient *
 * by using the fact that pins appear first in the block list,    *
 * but this shouldn't cause any significant slowdown and won't be *
 * broken if I ever change the parser so that the pins aren't     *
 * necessarily at the start of the block list.                    */

 if (fixed_pins == TRUE) {
    while (block[b_from].type != CLB) {
       b_from = my_irand(num_blocks - 1);
    }
 }

 x_from = block[b_from].x;
 y_from = block[b_from].y;
 find_to (x_from, y_from, block[b_from].type, rlim, &x_to, &y_to);

/* Make the switch in order to make computing the new bounding *
 * box simpler.  If the cost increase is too high, switch them *
 * back.  (block data structures switched, clbs not switched   *
 * until success of move is determined.)                       */

 if (block[b_from].type == CLB) {
    io_num = -1;            /* Don't need, but stops compiler warning. */
    if (clb[x_to][y_to].occ == 1) {         /* Occupied -- do a switch */
       b_to = clb[x_to][y_to].u.block;
       block[b_from].x = x_to;
       block[b_from].y = y_to;
       block[b_to].x = x_from;
       block[b_to].y = y_from; 
    }    
    else {
#define EMPTY -1      
       b_to = EMPTY;
       block[b_from].x = x_to;
       block[b_from].y = y_to; 
    }
 }
 else {   /* io block was selected for moving */
    io_num = my_irand(io_rat - 1);
    if (io_num >= clb[x_to][y_to].occ) {  /* Moving to an empty location */
       b_to = EMPTY;
       block[b_from].x = x_to;
       block[b_from].y = y_to;
    }
    else {          /* Swapping two blocks */
       b_to = *(clb[x_to][y_to].u.io_blocks+io_num);
       block[b_to].x = x_from;
       block[b_to].y = y_from;
       block[b_from].x = x_to;
       block[b_from].y = y_to;
    }

 }

/* Now update the cost function.  May have to do major optimizations *
 * here later.                                                       */

/* I'm using negative values of tempcost as a flag, so DO NOT        *
 * use cost functions that can go negative.                          */

 delta_c = 0;                    /* Change in cost due to this swap. */
 num_of_pins = pins_on_block[block[b_from].type];    

 num_nets_affected = find_affected_nets (nets_to_update, net_block_moved, 
     b_from, b_to, num_of_pins);

 if (place_cost_type == NONLINEAR_CONG) {
    save_region_occ (old_region_occ_x, old_region_occ_y, num_regions);
 }

 bb_index = 0;               /* Index of new bounding box. */

 for (k=0;k<num_nets_affected;k++) {
    inet = nets_to_update[k];

/* If we swapped two blocks connected to the same net, its bounding box *
 * doesn't change.                                                      */

    if (net_block_moved[k] == FROM_AND_TO) 
       continue;

    if (net[inet].num_pins <= SMALL_NET) {
       get_non_updateable_bb (inet, &bb_coord_new[bb_index]);
    }
    else {
       if (net_block_moved[k] == FROM) 
          update_bb (inet, &bb_coord_new[bb_index], &bb_edge_new[bb_index],
             x_from, y_from, x_to, y_to);      
       else
          update_bb (inet, &bb_coord_new[bb_index], &bb_edge_new[bb_index],
             x_to, y_to, x_from, y_from);      
    }

    if (place_cost_type != NONLINEAR_CONG) {
       net[inet].tempcost = net_cost (inet, &bb_coord_new[bb_index]);
       delta_c += net[inet].tempcost - net[inet].ncost;
    }
    else {
           /* Rip up, then replace with new bb. */
       update_region_occ (inet, &bb_coords[inet], -1, num_regions);  
       update_region_occ (inet, &bb_coord_new[bb_index],1, num_regions);
    }

    bb_index++;
 }   

 if (place_cost_type == NONLINEAR_CONG) {
    newcost = nonlinear_cong_cost(num_regions);
    delta_c = newcost - *cost;
 }

 keep_switch = assess_swap (delta_c, t); 

 /* 1 -> move accepted, 0 -> rejected. */ 

 if (keep_switch) {
    *cost = *cost + delta_c;

 /* update net cost functions and reset flags. */

    bb_index = 0;

    for (k=0;k<num_nets_affected;k++) {
       inet = nets_to_update[k];

/* If we swapped two blocks connected to the same net, its bounding box *
 * doesn't change.                                                      */

       if (net_block_moved[k] == FROM_AND_TO) {
          net[inet].tempcost = -1;  
          continue;
       }

       bb_coords[inet] = bb_coord_new[bb_index];
       if (net[inet].num_pins > SMALL_NET) 
          bb_num_on_edges[inet] = bb_edge_new[bb_index];

       bb_index++;

       net[inet].ncost = net[inet].tempcost;
       net[inet].tempcost = -1;  
    }

 /* Update Clb data structures since we kept the move. */

    if (block[b_from].type == CLB) {
       if (b_to != EMPTY) {
          clb[x_from][y_from].u.block = b_to; 
          clb[x_to][y_to].u.block = b_from;
       }
       else {
          clb[x_to][y_to].u.block = b_from;   
          clb[x_to][y_to].occ = 1;
          clb[x_from][y_from].occ = 0; 
       }
    }

    else {     /* io block was selected for moving */

     /* Get the "sub_block" number of the b_from block. */

       for (off_from=0;;off_from++) {
          if (clb[x_from][y_from].u.io_blocks[off_from] == b_from) break;
       }

       if (b_to != EMPTY) {   /* Swapped two blocks. */
          clb[x_to][y_to].u.io_blocks[io_num] = b_from;
          clb[x_from][y_from].u.io_blocks[off_from] = b_to;
       }
       else {                 /* Moved to an empty location */
          clb[x_to][y_to].u.io_blocks[clb[x_to][y_to].occ] = b_from;  
          clb[x_to][y_to].occ++;   
          for  (k=off_from;k<clb[x_from][y_from].occ-1;k++) { /* prevent gap  */
             clb[x_from][y_from].u.io_blocks[k] =          /* in io_blocks */
                clb[x_from][y_from].u.io_blocks[k+1];
          }
          clb[x_from][y_from].occ--;
       }
    }  
 }  

 else {    /* Move was rejected.  */

/* Reset the net cost function flags first. */
    for (k=0;k<num_nets_affected;k++) {
       inet = nets_to_update[k];
       net[inet].tempcost = -1;  
    }
    
 /* Restore the block data structures to their state before the move. */
    block[b_from].x = x_from;
    block[b_from].y = y_from;
    if (b_to != EMPTY) {
       block[b_to].x = x_to;
       block[b_to].y = y_to;
    }

/* Restore the region occupancies to their state before the move. */
    if (place_cost_type == NONLINEAR_CONG) {
       restore_region_occ (old_region_occ_x, old_region_occ_y, num_regions);
    }

 }
 
 return(keep_switch);
}


void save_region_occ (float **old_region_occ_x, float **old_region_occ_y,
      int num_regions) {

/* Saves the old occupancies of the placement subregions in case the  *
 * current move is not accepted.  Used only for NONLINEAR_CONG.       */

 int i, j;

 for (i=0;i<num_regions;i++) { 
    for (j=0;j<num_regions;j++) { 
       old_region_occ_x[i][j] = place_region_x[i][j].occupancy; 
       old_region_occ_y[i][j] = place_region_y[i][j].occupancy; 
    } 
 } 
}


void restore_region_occ (float **old_region_occ_x, float **old_region_occ_y,
       int num_regions) {

/* Restores the old occupancies of the placement subregions when the  *
 * current move is not accepted.  Used only for NONLINEAR_CONG.       */

 int i, j;
 
 for (i=0;i<num_regions;i++) {
    for (j=0;j<num_regions;j++) {
       place_region_x[i][j].occupancy = old_region_occ_x[i][j];
       place_region_y[i][j].occupancy = old_region_occ_y[i][j];
    }
 }
}


int find_affected_nets (int *nets_to_update, int *net_block_moved,
    int b_from, int b_to, int num_of_pins) {

/* Puts a list of all the nets connected to b_from and b_to into          *
 * nets_to_update.  Returns the number of affected nets.  Net_block_moved *
 * is either FROM, TO or FROM_AND_TO -- the block connected to this net   *
 * that has moved.                                                        */

 int k, inet, affected_index, count;

 affected_index = 0;

 for (k=0;k<num_of_pins;k++) {
    inet = block[b_from].nets[k];
    
    if (inet == OPEN) 
       continue;
 
    if (is_global[inet]) 
       continue;

/* This is here in case the same block connects to a net twice. */

    if (net[inet].tempcost > 0.)  
       continue;

    nets_to_update[affected_index] = inet;
    net_block_moved[affected_index] = FROM;
    affected_index++;
    net[inet].tempcost = 1.;           /* Flag to say we've marked this net. */
 }

 if (b_to != EMPTY) {
    for (k=0;k<num_of_pins;k++) {
       inet = block[b_to].nets[k];
    
       if (inet == OPEN) 
          continue;
 
       if (is_global[inet]) 
          continue;

       if (net[inet].tempcost > 0.) {         /* Net already marked. */
          for (count=0;count<affected_index;count++) {
             if (nets_to_update[count] == inet) {
                if (net_block_moved[count] == FROM) 
                   net_block_moved[count] = FROM_AND_TO;
                break;
             }
          }

#ifdef DEBUG
          if (count > affected_index) {
             printf("Error in find_affected_nets -- count = %d,"
              " affected index = %d.\n", count, affected_index);
             exit (1);
          }
#endif
       }
                 
       else {           /* Net not marked yet. */

          nets_to_update[affected_index] = inet;
          net_block_moved[affected_index] = TO;
          affected_index++;
          net[inet].tempcost = 1.;    /* Flag means we've  marked net. */
       }
    }
 }

 return (affected_index);
}


void find_to (int x_from, int y_from, int type, float rlim, int *x_to, 
  int *y_to) {
 /* Returns the point to which I want to swap, properly range limited. *
  * rlim must always be between 1 and nx (inclusive) for this routine  *
  * to work.                                                           */

 int x_rel, y_rel, iside, iplace, rlx, rly;

 rlx = min(nx,rlim);   /* Only needed when nx < ny. */
 rly = min (ny,rlim);  /* Added rly for aspect_ratio != 1 case. */

#ifdef DEBUG
 if (rlx < 1 || rlx > nx) {
    printf("Error in find_to: rlx = %d\n",rlx);
    exit(1);
 }
#endif

 do {              /* Until (x_to, y_to) different from (x_from, y_from) */
    if (type == CLB) {
       x_rel = my_irand (2*rlx);    
       y_rel = my_irand (2*rly);
       *x_to = x_from - rlx + x_rel;
       *y_to = y_from - rly + y_rel;
       if (*x_to > nx) *x_to = *x_to - nx;    /* better spectral props. */
       if (*x_to < 1) *x_to = *x_to + nx;     /* than simple min, max   */
       if (*y_to > ny) *y_to = *y_to - ny;    /* clipping.              */
       if (*y_to < 1) *y_to = *y_to + ny;
    }
    else {                 /* io_block to be moved. */
       if (rlx >= nx) {
          iside = my_irand(3);
/*                              *
 *       +-----1----+           *
 *       |          |           *
 *       |          |           *
 *       0          2           *
 *       |          |           *
 *       |          |           *
 *       +-----3----+           *
 *                              */
          switch (iside) {
          case 0:
             iplace = my_irand (ny-1) + 1;
             *x_to = 0;
             *y_to = iplace;
             break;
          case 1:
             iplace = my_irand (nx-1) + 1;
             *x_to = iplace;
             *y_to = ny+1;
             break;
          case 2:
             iplace = my_irand (ny-1) + 1;
             *x_to = nx+1;
             *y_to = iplace;
             break;
          case 3:
             iplace = my_irand (nx-1) + 1;
             *x_to = iplace;
             *y_to = 0;
             break;
          default:
             printf("Error in find_to.  Unexpected io swap location.\n");
             exit (1);
          }
       }
       else {   /* rlx is less than whole chip */
          if (x_from == 0) {
             iplace = my_irand (2*rly);
             *y_to = y_from - rly + iplace;
             *x_to = x_from;
             if (*y_to > ny) {
                *y_to = ny + 1;
                *x_to = my_irand (rlx - 1) + 1;
             }
             else if (*y_to < 1) {
                *y_to = 0;
                *x_to = my_irand (rlx - 1) + 1;
             }
          }
          else if (x_from == nx+1) {
             iplace = my_irand (2*rly);
             *y_to = y_from - rly + iplace;
             *x_to = x_from;
             if (*y_to > ny) {
                *y_to = ny + 1;
                *x_to = nx - my_irand (rlx - 1); 
             }
             else if (*y_to < 1) {
                *y_to = 0;
                *x_to = nx - my_irand (rlx - 1);
             }
          }
          else if (y_from == 0) {
             iplace = my_irand (2*rlx);
             *x_to = x_from - rlx + iplace;
             *y_to = y_from;
             if (*x_to > nx) {
                *x_to = nx + 1;
                *y_to = my_irand (rly - 1) + 1;
             }
             else if (*x_to < 1) {
                *x_to = 0;
                *y_to = my_irand (rly -1) + 1;
             }
          }
          else {  /* *y_from == ny + 1 */
             iplace = my_irand (2*rlx);
             *x_to = x_from - rlx + iplace;
             *y_to = y_from;
             if (*x_to > nx) {
                *x_to = nx + 1;
                *y_to = ny - my_irand (rly - 1);
             }
             else if (*x_to < 1) {
                *x_to = 0;
                *y_to = ny - my_irand (rly - 1);
             }
          }
       }    /* End rlx if */
    }    /* end type if */
 } while ((x_from == *x_to) && (y_from == *y_to));

#ifdef DEBUG
   if (*x_to < 0 || *x_to > nx+1 || *y_to < 0 || *y_to > ny+1) {
      printf("Error in routine find_to:  (x_to,y_to) = (%d,%d)\n",
            *x_to, *y_to);
      exit(1);
   }

   if (type == CLB) {
     if (clb[*x_to][*y_to].type != CLB) {
        printf("Error: Moving CLB to illegal type block at (%d,%d)\n",
          *x_to,*y_to);
        exit(1);
     }
   }
   else {
     if (clb[*x_to][*y_to].type != IO) {
        printf("Error: Moving IO block to illegal type location at "
              "(%d,%d)\n", *x_to, *y_to);
        exit(1);
     }
   }
#endif

/* printf("(%d,%d) moved to (%d,%d)\n",x_from,y_from,*x_to,*y_to); */
}


int assess_swap (float delta_c, float t) {
/* Returns: 1 -> move accepted, 0 -> rejected. */ 

 int accept;
 float prob_fac, fnum;

 if (delta_c <= 0) {
    accept = 1;
    return(accept);
 }
 if (t == 0.) return(0);

 fnum = my_frand();
 prob_fac = exp(-delta_c/t);
 if (prob_fac > fnum) {
    accept = 1;
 }
 else {
    accept = 0;
 }
 return(accept);
}


float recompute_cost (int place_cost_type, int num_regions) {

/* Recomputes the cost to eliminate roundoff that may have accrued.  *
 * This routine does as little work as possible to compute this new  *
 * cost.                                                             */

 int i, j, inet;
 float cost;
 void update_region_occ (int inet, struct s_bb*coords, int add_or_sub,
       int num_regions);
 float nonlinear_cong_cost (int num_regions);

 cost = 0;

/* Initialize occupancies to zero if regions are being used. */
 
 if (place_cost_type == NONLINEAR_CONG) {
    for (i=0;i<num_regions;i++) {
       for (j=0;j<num_regions;j++) {
           place_region_x[i][j].occupancy = 0.;
           place_region_y[i][j].occupancy = 0.;
       }
    }
 }    

 for (inet=0;inet<num_nets;inet++) {     /* for each net ... */
 
    if (is_global[inet] == 0) {    /* Do only if not global. */

       /* Bounding boxes don't have to be recomputed; they're correct. */ 
  
       if (place_cost_type != NONLINEAR_CONG) {
          cost += net[inet].ncost;
       } 
       else {      /* Must be nonlinear_cong case. */
          update_region_occ (inet, &bb_coords[inet], 1, num_regions);
       } 
    }
 }
 
 if (place_cost_type == NONLINEAR_CONG) {
    cost = nonlinear_cong_cost (num_regions);
 }
 
 return (cost);
}


float comp_cost (int method, int place_cost_type, int num_regions) {

/* Finds the cost from scratch.  Done only when the placement   *
 * has been radically changed (i.e. after initial placement).   *
 * Otherwise find the cost change incrementally.  If method     *
 * check is NORMAL, we find bounding boxes that are updateable  *
 * for the larger nets.  If method is CHECK, all bounding boxes *
 * are found via the non_updateable_bb routine, to provide a    *
 * cost which can be used to check the correctness of the       *
 * other routine.                                               */

 int i, j, k; 
 float cost;
 void get_bb_from_scratch (int inet, struct s_bb *coords, 
       struct s_bb *num_on_edges); 
 void get_non_updateable_bb (int inet, struct s_bb *coords);
 float net_cost (int inet, struct s_bb *bb_coords);
 void update_region_occ (int inet, struct s_bb*coords, int add_or_sub,
       int num_regions);
 float nonlinear_cong_cost (int num_regions);

 cost = 0;

/* Initialize occupancies to zero if regions are being used. */

 if (place_cost_type == NONLINEAR_CONG) {
    for (i=0;i<num_regions;i++) {
       for (j=0;j<num_regions;j++) {
           place_region_x[i][j].occupancy = 0.;
           place_region_y[i][j].occupancy = 0.;
       }
    }
 }

 for (k=0;k<num_nets;k++) {     /* for each net ... */

    if (is_global[k] == 0) {    /* Do only if not global. */

/* Small nets don't use incremental updating on their bounding boxes, *
 * so they can use a fast bounding box calculator.                    */

       if (net[k].num_pins > SMALL_NET && method == NORMAL) {
          get_bb_from_scratch (k, &bb_coords[k], &bb_num_on_edges[k]);
       }
       else {
          get_non_updateable_bb (k, &bb_coords[k]);
       }
       
       if (place_cost_type != NONLINEAR_CONG) {
          net[k].ncost = net_cost(k, &bb_coords[k]);
          cost += net[k].ncost;
       }
       else {      /* Must be nonlinear_cong case. */
          update_region_occ (k, &bb_coords[k], 1, num_regions);
       }
    }
 }

 if (place_cost_type == NONLINEAR_CONG) {
    cost = nonlinear_cong_cost (num_regions);
 }

 return (cost);
}


float nonlinear_cong_cost (int num_regions) {

/* This routine computes the cost of a placement when the NONLINEAR_CONG *
 * option is selected.  It assumes that the occupancies of all the       *
 * placement subregions have been properly updated, and simply           *
 * computes the cost due to these occupancies by summing over all        *
 * subregions.  This will be inefficient for moves that don't affect     *
 * many subregions (i.e. small moves late in placement), esp. when there *
 * are a lot of subregions.  May recode later to update only affected    *
 * subregions.                                                           */

 float cost, tmp;
 int i, j;

 cost = 0.;

 for (i=0;i<num_regions;i++) {
    for (j=0;j<num_regions;j++) {

/* Many different cost metrics possible.  1st try:  */

       if (place_region_x[i][j].occupancy < place_region_x[i][j].capacity) {
          cost += place_region_x[i][j].occupancy * 
               place_region_x[i][j].inv_capacity;
       }
       else {  /* Overused region -- penalize. */

          tmp = place_region_x[i][j].occupancy * 
               place_region_x[i][j].inv_capacity;
          cost += tmp * tmp;
       }

       if (place_region_y[i][j].occupancy < place_region_y[i][j].capacity) {
          cost += place_region_y[i][j].occupancy * 
               place_region_y[i][j].inv_capacity;
       }
       else {  /* Overused region -- penalize. */

          tmp = place_region_y[i][j].occupancy * 
               place_region_y[i][j].inv_capacity;
          cost += tmp * tmp;
       }

    }
 }

 return (cost);
}


void update_region_occ (int inet, struct s_bb *coords, int add_or_sub,
    int num_regions) {

/* Called only when the place_cost_type is NONLINEAR_CONG.  If add_or_sub *
 * is 1, this uses the new net bounding box to increase the occupancy     *
 * of some regions.  If add_or_sub = - 1, it decreases the occupancy      *
 * by that due to this bounding box.                                      */

 float net_xmin, net_xmax, net_ymin, net_ymax, crossing; 
 float inv_region_len, inv_region_height;
 float inv_bb_len, inv_bb_height;
 float overlap_xlow, overlap_xhigh, overlap_ylow, overlap_yhigh;
 float y_overlap, x_overlap, x_occupancy, y_occupancy;
 int imin, imax, jmin, jmax, i, j;

 if (net[inet].num_pins > 50) {
    crossing = 2.7933 + 0.02616 * (net[inet].num_pins - 50);
 }
 else {  
    crossing = cross_count[net[inet].num_pins-1];
 }

 net_xmin = coords->xmin - 0.5;
 net_xmax = coords->xmax + 0.5;
 net_ymin = coords->ymin - 0.5;
 net_ymax = coords->ymax + 0.5;
 
/* I could precompute the two values below.  Should consider this. */

 inv_region_len = (float) num_regions / (float) nx;
 inv_region_height = (float) num_regions / (float) ny;

/* Get integer coordinates defining the rectangular area in which the *
 * subregions have to be updated.  Formula is as follows:  subtract   *
 * 0.5 from net_xmin, etc. to get numbers from 0 to nx or ny;         *
 * divide by nx or ny to scale between 0 and 1; multiply by           *
 * num_regions to scale between 0 and num_regions; and truncate to    *
 * get the final answer.                                              */

 imin = (int) (net_xmin - 0.5) * inv_region_len;
 imax = (int) (net_xmax - 0.5) * inv_region_len;
 imax = min (imax, num_regions - 1);       /* Watch for weird roundoff */

 jmin = (int) (net_ymin - 0.5) * inv_region_height;
 jmax = (int) (net_ymax - 0.5) * inv_region_height;
 jmax = min (jmax, num_regions - 1);       /* Watch for weird roundoff */
 
 inv_bb_len = 1. / (net_xmax - net_xmin);
 inv_bb_height = 1. / (net_ymax - net_ymin);

/* See RISA paper (DAC '94, pp. 690 - 695) for a description of why I *
 * use exactly this cost function.                                    */

 for (i=imin;i<=imax;i++) {
    for (j=jmin;j<=jmax;j++) {
       overlap_xlow = max (place_region_bounds_x[i],net_xmin);
       overlap_xhigh = min (place_region_bounds_x[i+1],net_xmax);
       overlap_ylow = max (place_region_bounds_y[j],net_ymin);
       overlap_yhigh = min (place_region_bounds_y[j+1],net_ymax);
       
       x_overlap = overlap_xhigh - overlap_xlow;
       y_overlap = overlap_yhigh - overlap_ylow;

#ifdef DEBUG

       if (x_overlap < -0.001) {
          printf ("Error in update_region_occ:  x_overlap < 0"
                  "\n inet = %d, overlap = %f\n", inet, x_overlap);
       }

       if (y_overlap < -0.001) {
          printf ("Error in update_region_occ:  y_overlap < 0"
                  "\n inet = %d, overlap = %f\n", inet, y_overlap);
       }
#endif


       x_occupancy = crossing * y_overlap * x_overlap * inv_bb_height *
             inv_region_len;
       y_occupancy = crossing * x_overlap * y_overlap * inv_bb_len *
             inv_region_height;
       
       place_region_x[i][j].occupancy += add_or_sub * x_occupancy;
       place_region_y[i][j].occupancy += add_or_sub * y_occupancy;
    }
 }

}


void alloc_place_regions (int num_regions) {

/* Allocates memory for the regional occupancy, cost, etc. counts *
 * kept when we're using the NONLINEAR_CONG placement cost        *
 * function.                                                      */

 place_region_x = (struct s_place_region **) alloc_matrix (0, num_regions-1,
   0, num_regions-1, sizeof (struct s_place_region));

 place_region_y = (struct s_place_region **) alloc_matrix (0, num_regions-1,
   0, num_regions-1, sizeof (struct s_place_region));
 
 place_region_bounds_x = (float *) my_malloc ((num_regions+1) * 
    sizeof (float));

 place_region_bounds_y = (float *) my_malloc ((num_regions+1) * 
    sizeof (float));
}


void load_place_regions (int num_regions) {

/* Loads the capacity values in each direction for each of the placement *
 * regions.  The chip is divided into a num_regions x num_regions array. */

 int i, j, low_block, high_block, rnum;
 float low_lim, high_lim, capacity, fac, block_capacity;
 float len_fac, height_fac;

/* First load up horizontal channel capacities.  */ 

 for (j=0;j<num_regions;j++) {
     capacity = 0.;
     low_lim = (float) j / (float) num_regions * ny + 1.;
     high_lim = (float) (j+1) / (float) num_regions * ny;
     low_block = floor (low_lim);
     low_block = max (1,low_block); /* Watch for weird roundoff effects. */
     high_block = ceil (high_lim);
     high_block = min(high_block, ny);

     block_capacity = (chan_width_x[low_block - 1] + 
          chan_width_x[low_block])/2.;
     if (low_block == 1) 
        block_capacity += chan_width_x[0]/2.;

     fac = 1. - (low_lim - low_block);
     capacity += fac * block_capacity;
        
     for (rnum=low_block+1;rnum<high_block;rnum++) {
        block_capacity = (chan_width_x[rnum-1] + chan_width_x[rnum]) / 2.;
        capacity += block_capacity;
     }

     block_capacity = (chan_width_x[high_block-1] +
           chan_width_x[high_block]) / 2.;
     if (high_block == ny) 
        block_capacity += chan_width_x[ny]/2.;

     fac = 1. - (high_block - high_lim);
     capacity += fac * block_capacity;

     for (i=0;i<num_regions;i++) {
        place_region_x[i][j].capacity = capacity;
        place_region_x[i][j].inv_capacity = 1. / capacity;
        place_region_x[i][j].occupancy = 0.;
        place_region_x[i][j].cost = 0.;
     }
 }

/* Now load vertical channel capacities.  */
 
 for (i=0;i<num_regions;i++) {
     capacity = 0.;
     low_lim = (float) i / (float) num_regions * nx + 1.;
     high_lim = (float) (i+1) / (float) num_regions * nx;
     low_block = floor (low_lim);
     low_block = max (1,low_block); /* Watch for weird roundoff effects. */
     high_block = ceil (high_lim);
     high_block = min(high_block, nx);
 
     block_capacity = (chan_width_y[low_block - 1] +
          chan_width_y[low_block])/2.;
     if (low_block == 1)
        block_capacity += chan_width_y[0]/2.;
 
     fac = 1. - (low_lim - low_block);
     capacity += fac * block_capacity;
  
     for (rnum=low_block+1;rnum<high_block;rnum++) {
        block_capacity = (chan_width_y[rnum-1] + chan_width_y[rnum]) / 2.;
        capacity += block_capacity;
     }
 
     block_capacity = (chan_width_y[high_block-1] +
           chan_width_y[high_block]) / 2.;
     if (high_block == nx)
        block_capacity += chan_width_y[nx]/2.;
 
     fac = 1. - (high_block - high_lim);
     capacity += fac * block_capacity;
 
     for (j=0;j<num_regions;j++) {
        place_region_y[i][j].capacity = capacity; 
        place_region_y[i][j].inv_capacity = 1. / capacity;
        place_region_y[i][j].occupancy = 0.;
        place_region_y[i][j].cost = 0.; 
     }
 }

/* Finally set up the arrays indicating the limits of each of the *
 * placement subregions.                                          */

 len_fac = (float) nx / (float) num_regions;
 height_fac = (float) ny / (float) num_regions;

 place_region_bounds_x[0] = 0.5;
 place_region_bounds_y[0] = 0.5;
 
 for (i=1;i<=num_regions;i++) {
    place_region_bounds_x[i] = place_region_bounds_x[i-1] + len_fac;
    place_region_bounds_y[i] = place_region_bounds_y[i-1] + height_fac;
 }
}


void alloc_and_load_unique_pin_list (void) {

/* This routine looks for multiple pins going to the same block in the *
 * pinlist of each net.  If it finds any, it marks that net as having  *
 * duplicate pins, and creates a new pinlist with no duplicates.  This *
 * is then used by the updatable bounding box calculation routine for  *
 * efficiency.                                                         */
  
 int inet, ipin, bnum, num_dup, any_dups, offset;
 int *times_listed;  /* [0..num_blocks-1]: number of times a block is   *
                      * listed in the pinlist of a net.  Temp. storage. */

 duplicate_pins = my_calloc (num_nets, sizeof(int));
 times_listed = my_calloc (num_blocks, sizeof(int)); 
 any_dups = 0;

 for (inet=0;inet<num_nets;inet++) {
    
    num_dup = 0;

    for (ipin=0;ipin<net[inet].num_pins;ipin++) {
       bnum = net[inet].pins[ipin];
       times_listed[bnum]++;
       if (times_listed[bnum] > 1) 
          num_dup++;
    }

    if (num_dup > 0) {   /* Duplicates found.  Make unique pin list. */
       duplicate_pins[inet] = num_dup;

       if (any_dups == 0) {        /* This is the first duplicate found */
          unique_pin_list = (int **) my_calloc (num_nets, sizeof(int *));
          any_dups = 1;
       }

       unique_pin_list[inet] = my_malloc ((net[inet].num_pins - num_dup) *
                sizeof(int));

       offset = 0;
       for (ipin=0;ipin<net[inet].num_pins;ipin++) { 
          bnum = net[inet].pins[ipin];
          if (times_listed[bnum] != 0) {
             times_listed[bnum] = 0;
             unique_pin_list[inet][offset] = bnum;
             offset++;
          }
       }
    }

    else {          /* No duplicates found.  Reset times_listed. */
       for (ipin=0;ipin<net[inet].num_pins;ipin++) {
          bnum = net[inet].pins[ipin];
          times_listed[bnum] = 0;
       }
    }
 }

 free ((void *) times_listed);
}


void get_bb_from_scratch (int inet, struct s_bb *coords, 
   struct s_bb *num_on_edges) {
/* This routine finds the bounding box of each net from scratch (i.e.    *
 * from only the block location information).  It updates both the       *
 * coordinate and number of blocks on each edge information.  It         *
 * should only be called when the bounding box information is not valid. */

 int ipin, bnum, x, y, xmin, xmax, ymin, ymax;
 int xmin_edge, xmax_edge, ymin_edge, ymax_edge;
 int n_pins;
 int *plist;

/* I need a list of blocks to which this net connects, with no block listed *
 * more than once, in order to get a proper count of the number on the edge *
 * of the bounding box.                                                     */

 if (duplicate_pins[inet] == 0) {
    plist = net[inet].pins;
    n_pins = net[inet].num_pins;
 }
 else {
    plist = unique_pin_list[inet];
    n_pins = net[inet].num_pins - duplicate_pins[inet];
 }

 x = block[plist[0]].x;
 y = block[plist[0]].y;

 x = max(min(x,nx),1);   
 y = max(min(y,ny),1);

 xmin = x;
 ymin = y;
 xmax = x;
 ymax = y;
 xmin_edge = 1;
 ymin_edge = 1;
 xmax_edge = 1;
 ymax_edge = 1;
 
 for (ipin=1;ipin<n_pins;ipin++) {
 
    bnum = plist[ipin];
    x = block[bnum].x;
    y = block[bnum].y;

/* Code below counts IO blocks as being within the 1..nx, 1..ny clb array. *
 * This is because channels do not go out of the 0..nx, 0..ny range, and   *
 * I always take all channels impinging on the bounding box to be within   *
 * that bounding box.  Hence, this "movement" of IO blocks does not affect *
 * the which channels are included within the bounding box, and it         *
 * simplifies the code a lot.                                              */

    x = max(min(x,nx),1);   
    y = max(min(y,ny),1);

    if (x == xmin) {  
       xmin_edge++;
    }
    if (x == xmax) {  /* Recall that xmin could equal xmax -- don't use else */
       xmax_edge++;
    }
    else if (x < xmin) {
       xmin = x;
       xmin_edge = 1;
    }
    else if (x > xmax) {
       xmax = x;
       xmax_edge = 1;
    }

    if (y == ymin) {
       ymin_edge++;
    }
    if (y == ymax) {
       ymax_edge++;
    }
    else if (y < ymin) {
       ymin = y;
       ymin_edge = 1;
    }
    else if (y > ymax) {
       ymax = y;
       ymax_edge = 1;
    }
 }

/* Copy the coordinates and number on edges information into the proper   *
 * structures.                                                            */

 coords->xmin = xmin;
 coords->xmax = xmax;
 coords->ymin = ymin;
 coords->ymax = ymax;

 num_on_edges->xmin = xmin_edge;
 num_on_edges->xmax = xmax_edge;
 num_on_edges->ymin = ymin_edge;
 num_on_edges->ymax = ymax_edge;
}


float net_cost (int inet, struct s_bb *bbptr) {
/* Finds the cost due to one net by looking at its coordinate bounding  *
 * box.                                                                 */

 float ncost, crossing;
   
/* Get the expected "crossing count" of a net, based on its number *
 * of pins.  Extrapolate for very large nets.                      */

 if (net[inet].num_pins > 50) {
    crossing = 2.7933 + 0.02616 * (net[inet].num_pins - 50); 
/*    crossing = 3.0;    Old value  */
 }
 else {
    crossing = cross_count[net[inet].num_pins-1]; 
 }

/* Could insert a check for xmin == xmax.  In that case, assume  *
 * connection will be made with no bends and hence no x-cost.    *
 * Same thing for y-cost.                                        */

/* Cost = wire length along channel * cross_count / average      *
 * channel capacity.   Do this for x, then y direction and add.  */

 ncost = (bbptr->xmax - bbptr->xmin + 1) * crossing *
         chanx_place_cost_fac[bbptr->ymax][bbptr->ymin-1]; 

 ncost += (bbptr->ymax - bbptr->ymin + 1) * crossing *
          chany_place_cost_fac[bbptr->xmax][bbptr->xmin-1];

 return(ncost);
}


void get_non_updateable_bb (int inet, struct s_bb *bb_coord_new) {
/* Finds the bounding box of a net and stores its coordinates in the  *
 * bb_coord_new data structure.  This routine should only be called   *
 * for small nets, since it does not determine enough information for *
 * the bounding box to be updated incrementally later.                *
 * Currently assumes channels on both sides of the CLBs forming the   *
 * edges of the bounding box can be used.  Essentially, I am assuming *
 * the pins always lie on the outside of the bounding box.            */


 int k, xmax, ymax, xmin, ymin, x, y;
 
 x = block[net[inet].pins[0]].x;
 y = block[net[inet].pins[0]].y;

 xmin = x;
 ymin = y;
 xmax = x;
 ymax = y;

 for (k=1;k<net[inet].num_pins;k++) {
    x = block[net[inet].pins[k]].x;
    y = block[net[inet].pins[k]].y;

    if (x < xmin) {
       xmin = x;
    }
    else if (x > xmax) {
       xmax = x;
    }

    if (y < ymin) {
       ymin = y;
    }
    else if (y > ymax ) {
       ymax = y;
    }
 }

 /* Now I've found the coordinates of the bounding box.  There are no *
  * channels beyond nx and ny, so I want to clip to that.  As well,   *
  * since I'll always include the channel immediately below and the   *
  * channel immediately to the left of the bounding box, I want to    *
  * clip to 1 in both directions as well (since minimum channel index *
  * is 0).  See route.c for a channel diagram.                        */
 
 bb_coord_new->xmin = max(min(xmin,nx),1);
 bb_coord_new->ymin = max(min(ymin,ny),1);
 bb_coord_new->xmax = max(min(xmax,nx),1);
 bb_coord_new->ymax = max(min(ymax,ny),1);
}


void update_bb (int inet, struct s_bb *bb_coord_new, struct s_bb *bb_edge_new,
    int xold, int yold, int xnew, int ynew) {
/* Updates the bounding box of a net by storing its coordinates in    *
 * the bb_coord_new data structure and the number of blocks on each   *
 * edge in the bb_edge_new data structure.  This routine should only  *
 * be called for large nets, since it has some overhead relative to   *
 * just doing a brute force bounding box calculation.  The bounding   *
 * box coordinate and edge information for inet must be valid before  *
 * this routine is called.                                            *
 * Currently assumes channels on both sides of the CLBs forming the   *
 * edges of the bounding box can be used.  Essentially, I am assuming *
 * the pins always lie on the outside of the bounding box.            */

/* IO blocks are considered to be one cell in for simplicity. */

 xnew = max(min(xnew,nx),1);
 ynew = max(min(ynew,ny),1);
 xold = max(min(xold,nx),1);
 yold = max(min(yold,ny),1);

/* Check if I can update the bounding box incrementally. */ 

 if (xnew < xold) {                          /* Move to left. */

/* Update the xmax fields for coordinates and number of edges first. */

    if (xold == bb_coords[inet].xmax) {       /* Old position at xmax. */
       if (bb_num_on_edges[inet].xmax == 1) {
          get_bb_from_scratch (inet, bb_coord_new, bb_edge_new);
          return;
       }
       else {
          bb_edge_new->xmax = bb_num_on_edges[inet].xmax - 1;
          bb_coord_new->xmax = bb_coords[inet].xmax; 
       }
    }

    else {              /* Move to left, old postion was not at xmax. */
       bb_coord_new->xmax = bb_coords[inet].xmax; 
       bb_edge_new->xmax = bb_num_on_edges[inet].xmax;
    }

/* Now do the xmin fields for coordinates and number of edges. */

    if (xnew < bb_coords[inet].xmin) {    /* Moved past xmin */
       bb_coord_new->xmin = xnew;
       bb_edge_new->xmin = 1;
    }
    
    else if (xnew == bb_coords[inet].xmin) {   /* Moved to xmin */
       bb_coord_new->xmin = xnew;
       bb_edge_new->xmin = bb_num_on_edges[inet].xmin + 1;
    }
    
    else {                                  /* Xmin unchanged. */
       bb_coord_new->xmin = bb_coords[inet].xmin;
       bb_edge_new->xmin = bb_num_on_edges[inet].xmin;
    }
 }    /* End of move to left case. */


 else if (xnew > xold) {             /* Move to right. */
    
/* Update the xmin fields for coordinates and number of edges first. */

    if (xold == bb_coords[inet].xmin) {   /* Old position at xmin. */
       if (bb_num_on_edges[inet].xmin == 1) {
          get_bb_from_scratch (inet, bb_coord_new, bb_edge_new);
          return;
       }
       else {
          bb_edge_new->xmin = bb_num_on_edges[inet].xmin - 1;
          bb_coord_new->xmin = bb_coords[inet].xmin;
       }
    }

    else {                /* Move to right, old position was not at xmin. */
       bb_coord_new->xmin = bb_coords[inet].xmin;
       bb_edge_new->xmin = bb_num_on_edges[inet].xmin;
    }

/* Now do the xmax fields for coordinates and number of edges. */

    if (xnew > bb_coords[inet].xmax) {    /* Moved past xmax. */
       bb_coord_new->xmax = xnew;
       bb_edge_new->xmax = 1;   
    } 
    
    else if (xnew == bb_coords[inet].xmax) {   /* Moved to xmax */
       bb_coord_new->xmax = xnew;
       bb_edge_new->xmax = bb_num_on_edges[inet].xmax + 1;
    } 
     
    else {                                  /* Xmax unchanged. */ 
       bb_coord_new->xmax = bb_coords[inet].xmax; 
       bb_edge_new->xmax = bb_num_on_edges[inet].xmax;   
    }
 }    /* End of move to right case. */

 else {          /* xnew == xold -- no x motion. */
    bb_coord_new->xmin = bb_coords[inet].xmin;
    bb_coord_new->xmax = bb_coords[inet].xmax;
    bb_edge_new->xmin = bb_num_on_edges[inet].xmin;
    bb_edge_new->xmax = bb_num_on_edges[inet].xmax;
 }

/* Now account for the y-direction motion. */

 if (ynew < yold) {                  /* Move down. */

/* Update the ymax fields for coordinates and number of edges first. */

    if (yold == bb_coords[inet].ymax) {       /* Old position at ymax. */
       if (bb_num_on_edges[inet].ymax == 1) {
          get_bb_from_scratch (inet, bb_coord_new, bb_edge_new);
          return;
       }
       else {
          bb_edge_new->ymax = bb_num_on_edges[inet].ymax - 1;
          bb_coord_new->ymax = bb_coords[inet].ymax;
       }
    }     
       
    else {              /* Move down, old postion was not at ymax. */
       bb_coord_new->ymax = bb_coords[inet].ymax;
       bb_edge_new->ymax = bb_num_on_edges[inet].ymax;
    }     
 
/* Now do the ymin fields for coordinates and number of edges. */
 
    if (ynew < bb_coords[inet].ymin) {    /* Moved past ymin */
       bb_coord_new->ymin = ynew;
       bb_edge_new->ymin = 1;
    }     
    
    else if (ynew == bb_coords[inet].ymin) {   /* Moved to ymin */
       bb_coord_new->ymin = ynew;
       bb_edge_new->ymin = bb_num_on_edges[inet].ymin + 1;
    }     
    
    else {                                  /* ymin unchanged. */
       bb_coord_new->ymin = bb_coords[inet].ymin;
       bb_edge_new->ymin = bb_num_on_edges[inet].ymin;
    }     
 }    /* End of move down case. */
 
 else if (ynew > yold) {             /* Moved up. */
    
/* Update the ymin fields for coordinates and number of edges first. */
 
    if (yold == bb_coords[inet].ymin) {   /* Old position at ymin. */
       if (bb_num_on_edges[inet].ymin == 1) {
          get_bb_from_scratch (inet, bb_coord_new, bb_edge_new);
          return;
       }
       else {
          bb_edge_new->ymin = bb_num_on_edges[inet].ymin - 1;
          bb_coord_new->ymin = bb_coords[inet].ymin;
       }
    }     
       
    else {                /* Moved up, old position was not at ymin. */
       bb_coord_new->ymin = bb_coords[inet].ymin;
       bb_edge_new->ymin = bb_num_on_edges[inet].ymin;
    }     
 
/* Now do the ymax fields for coordinates and number of edges. */
 
    if (ynew > bb_coords[inet].ymax) {    /* Moved past ymax. */
       bb_coord_new->ymax = ynew;
       bb_edge_new->ymax = 1;
    }     
    
    else if (ynew == bb_coords[inet].ymax) {   /* Moved to ymax */
       bb_coord_new->ymax = ynew;
       bb_edge_new->ymax = bb_num_on_edges[inet].ymax + 1;
    }     
     
    else {                                  /* ymax unchanged. */
       bb_coord_new->ymax = bb_coords[inet].ymax;
       bb_edge_new->ymax = bb_num_on_edges[inet].ymax;
    }     
 }    /* End of move up case. */

 else {          /* ynew == yold -- no y motion. */
    bb_coord_new->ymin = bb_coords[inet].ymin;
    bb_coord_new->ymax = bb_coords[inet].ymax;
    bb_edge_new->ymin = bb_num_on_edges[inet].ymin;
    bb_edge_new->ymax = bb_num_on_edges[inet].ymax;
 }
}


void init_place(void) {  
/* Randomly places the blocks to create an initial placement.     */

 struct s_pos {int x; int y;} *pos; 
 int i, j, k, count, iblk, choice, tsize;

 tsize = max(nx*ny, 2*(nx+ny));
 pos = (struct s_pos *) my_malloc(tsize*sizeof(struct s_pos));
 /* Initialize all occupancy to zero. */
 for (i=0;i<=nx+1;i++) {
    for (j=0;j<=ny+1;j++) {
       clb[i][j].occ = 0;
    }
 }
 
 count = 0;
 for (i=1;i<=nx;i++) {
    for (j=1;j<=ny;j++) {
        pos[count].x = i;
        pos[count].y = j;
        count++;
     }
 }
 for (iblk=0;iblk<num_blocks;iblk++) {
    if (block[iblk].type == CLB) {     /* only place CLBs in center */
       choice = my_irand(count - 1); 
       clb[pos[choice].x][pos[choice].y].u.block = iblk;
       clb[pos[choice].x][pos[choice].y].occ = 1;
       /* Ensure randomizer doesn't pick this block again */
       pos[choice] = pos[count-1];   /* overwrite used block position */
       count--;
    }
 }

 /* Now do the io blocks around the periphery */
 count = 0;
 for (i=1;i<=nx;i++) {
    pos[count].x = i;
    pos[count].y = 0;
    pos[count+1].x = i;
    pos[count+1].y = ny + 1;
    count += 2;
 }
 for (j=1;j<=ny;j++) {
    pos[count].x = 0;
    pos[count].y = j;
    pos[count+1].x = nx + 1;
    pos[count+1].y = j;
    count += 2;
 }
 for (iblk=0;iblk<num_blocks;iblk++) {
    if (block[iblk].type == INPAD || block[iblk].type == OUTPAD) {
       choice = my_irand (count - 1); 
       *(clb[pos[choice].x][pos[choice].y].u.io_blocks + 
          clb[pos[choice].x][pos[choice].y].occ) = iblk;
       clb[pos[choice].x][pos[choice].y].occ++;
       if (clb[pos[choice].x][pos[choice].y].occ == io_rat) {
          /* Ensure randomizer doesn't pick this block again */
          pos[choice] = pos[count-1];   /* overwrite used block position */
          count--;
       }

    } 
 }

 for (i=0;i<=nx+1;i++) {
    for (j=0;j<=ny+1;j++) {
       if (clb[i][j].type == CLB) {
          if (clb[i][j].occ == 1) {
             block[clb[i][j].u.block].x = i;
             block[clb[i][j].u.block].y = j;
          }
       }
       else {
          if (clb[i][j].type == IO) {
             for (k=0;k<clb[i][j].occ;k++) {
                block[clb[i][j].u.io_blocks[k]].x = i;
                block[clb[i][j].u.io_blocks[k]].y = j;
             }
          }
       }
    }
 }

/* Below looks like dead code.  */

/* for (i=1;i<=nx;i++) {
    pos[i-1].x = i;
    pos[i-1].y = 0;
    pos[i-1+nx+ny].x = i;
    pos[i-1+nx+ny].y = ny + 1;
 }
 for (j=1;j<=ny;j++) {
    pos[count].x = 0;
    pos[count].y = j;
    pos[count+1].x = nx + 1;
    pos[count+1].y = j;
 }  */
 
#ifdef VERBOSE 
 printf("At end of init_place.\n");
 dump_clbs();
#endif

 free ((void *) pos);
 
}


void dump_clbs (void) {
/* Output routine for debugging.                       */

 int i, j, index;

 for (i=0;i<=nx+1;i++) {
    for (j=0;j<=ny+1;j++) {
       printf("clb (%d,%d):  type: %d  occ: %d\n",
        i,j,clb[i][j].type, clb[i][j].occ);
       if (clb[i][j].type == CLB) 
          printf("block: %d\n",clb[i][j].u.block);
       if (clb[i][j].type == IO) {
          printf("io_blocks: ");
          for (index=0;index<clb[i][j].occ;index++) 
              printf("%d  ", clb[i][j].u.io_blocks[index]);
          printf("\n");
       }
    }
 }

 for (i=0;i<num_blocks;i++) {
    printf("block: %d, (i,j): (%d, %d)\n",i,block[i].x,block[i].y);
 }
}


void init_chan (int cfactor) {
/* Assigns widths to channels (in tracks).  Minimum one track          *
 * per channel.  io channels are io_rat * maximum in interior          *
 * tracks wide.  The channel distributions read from the architecture  *
 * file are scaled by cfactor.                                         */

 float x, separation;
 int nio, i;
 float comp_width(struct s_chan *chan, float x, float separation);
 
/* io channel widths */

 nio = (int) floor (cfactor*chan_width_io + 0.5); 
 if (nio == 0) nio = 1;   /* No zero width channels */

 chan_width_x[0] = chan_width_x[ny] = nio;
 chan_width_y[0] = chan_width_y[nx] = nio;

 if (ny > 1) {  
    separation = 1./(ny-2.); /* Norm. distance between two channels. */
    x = 0.;    /* This avoids div by zero if ny = 2. */
    chan_width_x[1] = (int) floor (cfactor*comp_width(&chan_x_dist, x,
                   separation) + 0.5);

  /* No zero width channels */
    chan_width_x[1] = max(chan_width_x[1],1); 

    for (i=1;i<ny-1;i++) {
       x = (float) i/((float) (ny-2.)); 
       chan_width_x[i+1] = (int) floor (cfactor*comp_width(&chan_x_dist, x,
                   separation) + 0.5);
       chan_width_x[i+1] = max(chan_width_x[i+1],1);
    }
 }

 if (nx > 1) {
    separation = 1./(nx-2.); /* Norm. distance between two channels. */
    x = 0.;    /* Avoids div by zero if nx = 2. */
    chan_width_y[1] = (int) floor (cfactor*comp_width(&chan_y_dist, x,
                 separation) + 0.5);

    chan_width_y[1] = max(chan_width_y[1],1);

    for (i=1;i<nx-1;i++) {
       x = (float) i/((float) (nx-2.)); 
       chan_width_y[i+1] = (int) floor (cfactor*comp_width(&chan_y_dist, x,
                 separation) + 0.5);
       chan_width_y[i+1] = max(chan_width_y[i+1],1);
    }
 }

#ifdef VERBOSE 
    printf("\nchan_width_x:\n");
    for (i=0;i<=ny;i++) 
       printf("%d  ",chan_width_x[i]);
    printf("\n\nchan_width_y:\n");
    for (i=0;i<=nx;i++) 
       printf("%d  ",chan_width_y[i]);
    printf("\n\n");
#endif

}


float comp_width(struct s_chan *chan, float x, float separation) {
/* Return the relative channel density.  *chan points to a channel   *
 * functional description data structure, and x is the distance      *
 * (between 0 and 1) we are across the chip.  separation is the      *
 * distance between two channels, in the 0 to 1 coordinate system.   */

 float val;   

 switch (chan->type) {

 case UNIFORM:
    val = chan->peak; 
    break;

 case GAUSSIAN:
    val = (x - chan->xpeak)*(x - chan->xpeak)/(2*chan->width*
        chan->width);
    val = chan->peak*exp(-val);
    val += chan->dc;
    break;

 case PULSE:
    val = (float) fabs((double)(x - chan->xpeak));
    if (val > chan->width/2.) {
       val = 0;
    }
    else {
       val = chan->peak;
    }
    val += chan->dc;
    break;

 case DELTA:
    val = x - chan->xpeak;
    if (val > -separation / 2. && val <= separation / 2.)
       val = chan->peak;
    else 
       val = 0.;
    val += chan->dc;
    break;

 default:
    printf("Error in comp_width:  Unknown channel type %d.\n",chan->type);
    exit (1);
    break;
 }

 return(val);
}


void load_for_fast_cost_update (float place_cost_exp) {
/* This routine loads the chanx_place_cost_fac and chany_place_cost_fac *
 * arrays with the inverse of the average number of tracks per channel  *
 * between [subhigh] and [sublow].  This is only useful for the cost    *
 * function that takes the length of the net bounding box in each       *
 * dimension divided by the average number of tracks in that direction. *
 * For other cost functions, you don't have to bother calling this      *
 * routine; when using the cost function described above, however, you  *
 * must always call this routine after you call init_chan and before    *
 * you do any placement cost determination.  The place_cost_exp factor  *
 * specifies to what power the width of the channel should be taken --  *
 * larger numbers make narrower channels more expensive.                */

 int low, high;

/* First compute the number of tracks between channel high and channel *
 * low, inclusive, in an efficient manner.                             */

 chanx_place_cost_fac[0][0] = chan_width_x[0];

 for (high=1;high<=ny;high++) {
    chanx_place_cost_fac[high][high] = chan_width_x[high];    
    for (low=0;low<high;low++) {
       chanx_place_cost_fac[high][low] = chanx_place_cost_fac[high-1][low]
          + chan_width_x[high];
    }
 }

/* Now compute the inverse of the average number of tracks per channel *
 * between high and low.  The cost function divides by the average     *
 * number of tracks per channel, so by storing the inverse I convert   *
 * this to a faster multiplication.  Take this final number to the     *
 * place_cost_exp power -- numbers other than one mean this is no      *
 * longer a simple "average number of tracks"; it is some power of     *
 * that, allowing greater penalization of narrow channels.             */
 
 for (high=0;high<=ny;high++) 
    for (low=0;low<=high;low++) {
       chanx_place_cost_fac[high][low] = (high - low + 1.) / 
           chanx_place_cost_fac[high][low];
       chanx_place_cost_fac[high][low] = 
          pow ((double) chanx_place_cost_fac[high][low],
          (double) place_cost_exp);
    }


/* Now do the same thing for the y-directed channels.  First get the  *
 * number of tracks between channel high and channel low, inclusive.  */
 
 chany_place_cost_fac[0][0] = chan_width_y[0];
 
 for (high=1;high<=nx;high++) {
    chany_place_cost_fac[high][high] = chan_width_y[high];
    for (low=0;low<high;low++) {
       chany_place_cost_fac[high][low] = chany_place_cost_fac[high-1][low]
          + chan_width_y[high];
    }
 }
 
/* Now compute the inverse of the average number of tracks per channel * 
 * between high and low.  Take to specified power.                     */
 
 for (high=0;high<=nx;high++) 
    for (low=0;low<=high;low++) {
       chany_place_cost_fac[high][low] = (high - low + 1.) / 
           chany_place_cost_fac[high][low]; 
       chany_place_cost_fac[high][low] = 
          pow ((double) chany_place_cost_fac[high][low],
          (double) place_cost_exp);
    }

}


void check_place (float cost, int place_cost_type, int num_regions) {

/* Checks that the placement has not confused our data structures. *
 * i.e. the clb and block structures agree about the locations of  *
 * every block, blocks are in legal spots, etc.  Also recomputes   *
 * the final placement cost from scratch and makes sure it is      *
 * within roundoff of what we think the cost is.                   */

 static int *bdone; 
 int i, j, k, error=0, bnum;
 float cost_check;
 float comp_cost (int method, int place_cost_type, int num_regions);

 cost_check = comp_cost(CHECK, place_cost_type, num_regions);
 printf("Cost recomputed from scratch is %f.\n", cost_check);
 if (fabs(cost_check - cost) > cost * ERROR_TOL) {
    printf("Error:  cost_check: %f and cost: %f differ in check_place.\n",
      cost_check,cost);
    error++;
 }

 bdone = (int *) my_malloc (num_blocks*sizeof(int));
 for (i=0;i<num_blocks;i++) bdone[i] = 0;

/* Step through clb array. Check it against block array. */
 for (i=0;i<=nx+1;i++) 
    for (j=0;j<=ny+1;j++) {
       if (clb[i][j].occ == 0) continue;
       if (clb[i][j].type == CLB) {
          bnum = clb[i][j].u.block;
          if (block[bnum].type != CLB) {
             printf("Error:  block %d type does not match clb(%d,%d) type.\n",
               bnum,i,j);
             error++;
          }
          if ((block[bnum].x != i) || (block[bnum].y != j)) {
             printf("Error:  block %d location conflicts with clb(%d,%d)"
                "data.\n", bnum, i, j);
             error++;
          }
          if (clb[i][j].occ > 1) {
             printf("Error: clb(%d,%d) has occupancy of %d\n",
                i,j,clb[i][j].occ);
             error++;
          }
          bdone[bnum]++;
       }
       else {  /* IO block */
          if (clb[i][j].occ > io_rat) {
             printf("Error:  clb(%d,%d) has occupancy of %d\n",i,j,
                clb[i][j].occ);
             error++;
          }
          for (k=0;k<clb[i][j].occ;k++) {
             bnum = clb[i][j].u.io_blocks[k];
             if ((block[bnum].type != INPAD) && block[bnum].type != OUTPAD) {
               printf("Error:  block %d type does not match clb(%d,%d) type.\n",
                 bnum,i,j);
               error++;
             }
             if ((block[bnum].x != i) || (block[bnum].y != j)) {
                printf("Error:  block %d location conflicts with clb(%d,%d)"
                  "data.\n", bnum, i, j);
                error++; 
             }
             bdone[bnum]++;
          } 
       } 

    }

/* Check that every block exists in the clb and block arrays somewhere. */
 for (i=0;i<num_blocks;i++) 
    if (bdone[i] != 1) {
       printf("Error:  block %d listed %d times in data structures.\n",
          i,bdone[i]);
       error++;
    }
 free ((void *) bdone);

 if (error == 0) {
    printf("\nCompleted placement consistency check successfully.\n\n");
 }
 else {
    printf("\nCompleted placement consistency check, %d Errors found.\n\n",
       error);
    printf("Aborting program.\n");
    exit(1);
 }
}


void print_place (char *place_file, char *net_file, char *arch_file) {
/* Prints out the placement of the circuit.  The architecture and    *
 * netlist files used to generate this placement are recorded in the *
 * file to avoid loading a placement with the wrong support files    *
 * later.                                                            */

 FILE *fp; 
 int i, subblock;
 int get_subblock (int i, int j, int bnum);

 fp = my_open(place_file,"w",0);

 fprintf(fp,"Netlist file: %s   Architecture file: %s\n", net_file,
    arch_file);

 fprintf(fp,"block\tx\ty\tname\tsubblock\n");
 for (i=0;i<num_blocks;i++) {
    fprintf(fp,"%d\t%d\t%d\t%s",i, block[i].x, block[i].y, block[i].name);
    if (block[i].type == CLB) {
       fprintf(fp,"\t%d\n", 0);        /* Sub block number not meaningful. */
    }
    else {                /* IO block.  Save sub block number. */
       subblock = get_subblock (block[i].x, block[i].y, i);
       fprintf(fp,"\t%d\n", subblock);
    }
 }
 
 fclose(fp);
}


int get_subblock (int i, int j, int bnum) {
/* Use this routine only for IO blocks.  It passes back the index of the *
 * subblock containing block bnum at location (i,j).                     */

 int k;

 for (k=0;k<io_rat;k++) {
    if (clb[i][j].u.io_blocks[k] == bnum) 
       return (k);
 }

 printf("Error in get_subblock.  Block %d is not at location (%d,%d)\n",
    bnum, i, j);
 exit (1);
}


void read_place (char *place_file, char *net_file, char *arch_file,
   float place_cost_exp, int place_cost_type, int num_regions,
   int chan_width_factor) {

/* Reads in a previously computed placement of the circuit.  It      *
 * checks that the placement corresponds to the current architecture *
 * and netlist file.                                                 */

 FILE *fp;
 char net_check[BUFSIZE], arch_check[BUFSIZE], bname[BUFSIZE];
 char msg[BUFSIZE];
 int i, j, bnum, bcheck, subblock;
 float cost;
 float comp_cost (int method, int place_cost_type, int num_regions);
 void check_place (float cost_check, int place_cost_type, int num_regions);
 void init_draw_coords (float clb_width);
 void init_chan (int cfactor);
 void load_for_fast_cost_update (float place_cost_exp);
 void load_place_regions (int num_regions);

 printf("Reading the placement from file %s.\n", place_file);
 fp = my_open (place_file, "r", 0);

 fscanf (fp, "%*s %*s %s %*s %*s %s", net_check, arch_check);
 
 if (strcmp(net_check, net_file) != 0) {
    printf("Error reading %s.  Placement generated with netlist file\n",
       place_file);
    printf("%s; current net file is %s.\n", net_check, net_file);
    exit (1);
 }

 if (strcmp(arch_check, arch_file) != 0) {
    printf("Error reading %s.  Placement generated with architecture "
       "file\n", place_file);
    printf("%s; current architecture file is %s.\n", arch_check, arch_file);
    exit (1);
 }

 fscanf (fp, "%*s %*s %*s %*s %*s");    /* Skip header line. */

 for (i=0;i<=nx+1;i++) 
    for (j=0;j<=ny+1;j++)
       clb[i][j].occ = 0;

 for (bnum=0;bnum<num_blocks;bnum++) {
    fscanf (fp, "%d %d %d %s %d", &bcheck, &i, &j, bname, &subblock);

    if (bnum != bcheck) {
       printf("Error in read_place.  Block %d has a numerical label\n",
          bnum);
       printf("of %d.\n", bcheck); 
       exit (1);
    }
    
    if (strcmp (block[bnum].name, bname) != 0) {
       printf("Error in read_place.  Block %d name mismatch.\n", bnum);
       printf("Expected %s, got %s.\n", block[bnum].name, bname);
       exit (1);
    }

    if (i < 0 || i > nx+1 || j < 0 || j > ny + 1) {
       printf("Error in read_place.  Block #%d (%s) location\n", bnum, bname);
       printf("(%d,%d) is out of range.\n", i, j);
       exit (1);
    }

    block[bnum].x = i;
    block[bnum].y = j;

    if (clb[i][j].type == CLB) {
       if (block[bnum].type != CLB) {
          printf("Error in read_place.  Attempt to place block #%d (%s) in\n",
               bnum, bname);
          printf("a logic block location (%d, %d).\n", i, j);
          exit (1);
       }
       clb[i][j].u.block = bnum;
       clb[i][j].occ++;
    }

    else if (clb[i][j].type == IO) { 
       if (block[bnum].type != INPAD && block[bnum].type != OUTPAD) { 
          printf("Error in read_place.  Attempt to place block #%d (%s) in\n",
               bnum, bname);
          printf("an IO block location (%d, %d).\n", i, j); 
          exit (1); 
       } 
       clb[i][j].u.io_blocks[subblock] = bnum;
       clb[i][j].occ = max (clb[i][j].occ, subblock+1);
    }

    else {    /* Block type was ILLEGAL or some unknown value */
       printf("Error in read_place.  Block #%d (%s) is in an illegal ",
           bnum, bname);
       printf("location.\nLocation specified: (%d,%d).\n", i, j);
       exit (1);
    }
 }

 fclose (fp);
 printf ("Successfully read %s.\n", place_file);

/* Load the channel occupancies and cost factors so that:   *
 * (1) the cost check will be OK, and                       *
 * (2) the geometry will draw correctly.                    */

 init_chan (chan_width_factor);

 if (place_cost_type != NONLINEAR_CONG) {
    load_for_fast_cost_update(place_cost_exp);
 }
 else {
    load_place_regions(num_regions);
 }

 /* Need this for check_place. */

 cost = comp_cost (NORMAL, place_cost_type, num_regions);   
 printf("Placement cost is %f.\n", cost);
 check_place (cost, place_cost_type, num_regions);

 pic_on_screen = PLACEMENT;
 init_draw_coords ((float) chan_width_factor);
   
 sprintf (msg, "Placement from file %s.  Cost %f.", place_file,
     cost);
 update_screen (MAJOR, msg);
}