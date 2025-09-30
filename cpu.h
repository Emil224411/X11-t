#ifndef CPU_H
#define CPU_H

#define N 250
#define SIZE ((N+2) * (N+2))

#define IX(x, y) ((x)+(N+2)*(y))
#define SWAP(x0, x) {float *tmp=x0;x0=x;x=tmp;}

void add_source(int n, float *x, float *s, float dt);
void set_bnd(int n, int b, float *x);
void diffuse(int n, int b, float *x, float *x0, float diff, float dt);
void advect(int n, int b, float *d, float *d0, float *u, float *v, float dt) ;
void project(int n, float *u, float *v, float *p, float *div);
void dens_step(int n, float *x, float *x0, float *u, float *v, float diff, float dt);
void vel_step(int n, float *u, float *v, float *u0, float *v0, float visc, float dt);

#endif//CPU_H
