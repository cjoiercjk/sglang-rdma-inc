#ifndef __MY_ERROR_H__
#define __MY_ERROR_H__

// run with check and execute

#define rwce(run_exp, chk_exp, exec_exp) \
    { \
        run_exp; \
        if(chk_exp) { \
            fprintf(stderr, "Error on %s line %d:\n  ", __FILE__, __LINE__); \
            exec_exp; \
            exit(-1); \
        } \
    }
#define rwcm(run_exp, chk_exp, err_msg) rwce(run_exp, chk_exp, fprintf(stderr, "%s\n", err_msg))

#define rwc(run_exp, chk_exp) rwcm(run_exp, chk_exp, "NO ADDITIONAL INFO")
// Use assert(exp) instead if you don't want to check the expresion for release version.

#define MYCHECK(chk_exp, err_msg) rwcm(, chk_exp, err_msg)

#endif