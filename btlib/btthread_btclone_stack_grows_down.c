#include "btthread.h"

bool stack_grows_down(void * ptr_to_var_on_callers_stack) {
	char var_on_my_stack;
	
	return (char *)ptr_to_var_on_callers_stack > &var_on_my_stack;
}
