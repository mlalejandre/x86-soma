static char enorme[6u << 20];
int main(void) { enorme[5] = 1; return enorme[5]; }
