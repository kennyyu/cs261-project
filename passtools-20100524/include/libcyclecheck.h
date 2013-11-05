struct cyclecheck_node;

cyclecheck_node *cyclecheck_addnode(void *clientdata);
void cyclecheck_addedge(cyclecheck_node *anc, cyclecheck_node *dec, 
			void *clientdata);

void cyclecheck_check(void);

int cyclecheck_getdups(void);
int cyclecheck_getnumcycles(void);
// returns the edge clientdata of the edge that closes the cycle
void *cyclecheck_getonecycle(int which, ptrarray<void> &nodes_clientdata);
