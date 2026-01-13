// convert the contents of stdin to their ASCII values (e.g., 
// '\n' = 10) and spit out the <prog> array used in Figure 1 in
// Thompson's paper.
#include <stdio.h>

int main(void) { 
    // put your code here.
    char buf[10000];
    int n = 0;

    // Read input file and store in char array
    int c;
    while ((c = getchar()) != EOF) {
        buf[n] = c;
        n++;
    }
    buf[n] = '\0'; // null-terminate so we can use %s later

    printf("char prog[] = {\n");
	for(int i = 0; buf[i]; i++)
		printf("\t%d,%c", buf[i], (i+1)%8==0 ? '\n' : ' ');
	printf("0 };\n");
	printf("%s", buf);

	return 0;
}
