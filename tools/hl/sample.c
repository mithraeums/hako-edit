#include <stdio.h>
#define MAX(a, b) ((a) > (b) ? (a) : (b))
/* a block
   comment */
static const char *s = "esc \"q\" \x41\n";
int main(void) { char c = '\''; float f = 1.5e-3f; long h = 0x1Fu; return MAX(0, -1) // tail
}
