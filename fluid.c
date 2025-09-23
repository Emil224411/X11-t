#include "fluid.h"

void add_source(int n, float *to, float *from, float dt) 
{
    int size = (n+2)*(n+2);
	
    for (int i = 0; i < size; i++) 
		to[i] += dt*from[i];
}

void set_bnd(int n, int b, float *x) 
{
	for (int i = 1; i <= n; i++) {
		x[IX(  0,   i)] = b==1 ? -x[IX(1,i)] : x[IX(1,i)];
		x[IX(n+1,   i)] = b==1 ? -x[IX(n,i)] : x[IX(n,i)];
		x[IX(  i,   0)] = b==2 ? -x[IX(i,1)] : x[IX(i,1)];
		x[IX(  i, n+1)] = b==2 ? -x[IX(i,n)] : x[IX(i,n)];
	}
	x[IX(  0,   0)] = 0.5*(x[IX(1,   0)]+x[IX(  0, 1)]);
	x[IX(  0, n+1)] = 0.5*(x[IX(1, n+1)]+x[IX(  0, n)]);
	x[IX(n+1,   0)] = 0.5*(x[IX(n,   0)]+x[IX(n+1, 1)]);
	x[IX(n+1, n+1)] = 0.5*(x[IX(n, n+1)]+x[IX(n+1, n)]);
}


void diffuse(int n, int b, float *x, float *x0, float diff, float dt) 
{
    float a = dt * diff * n * n;

    for (int k = 0; k < 20; k++) {
        for (int i = 1; i <= n; i++) for (int j = 1; j <= n; j++) {
			x[IX(i, j)] = (x0[IX(i, j)] + a * 
							(x[IX(i-1, j  )] + x[IX(i+1, j  )] + 
							 x[IX(i  , j-1)] + x[IX(i  , j+1)])) / (1+4*a);
		}
		set_bnd(n, b, x);
    }
}

void advect(int n, int b, float *d, float *d0, float *u, float *v, float dt) 
{
	int i, j, i0, j0, i1, j1;
    float x, y, s0, t0, s1, t1, dt0;
	dt0 = dt * n;

    for (i = 1; i <= n; i++) {
        for (j = 1; j <= n; j++) {
            x = i - dt0 * u[IX(i, j)];
            y = j - dt0 * v[IX(i, j)];
            if (x < 0.5f) x = 0.5f; 
			if (x > n + 0.5f) x = n + 0.5f;
            if (y < 0.5f) y = 0.5f; 
			if (y > n + 0.5f) y = n + 0.5f;

            i0 = (int)x; i1 = i0 + 1;
            j0 = (int)y; j1 = j0 + 1;
            s1 = x - i0; s0 = 1 - s1;
            t1 = y - j0; t0 = 1 - t1;
            d[IX(i, j)] = s0 * (t0 * d0[IX(i0, j0)] + t1 * d0[IX(i0, j1)])+
                          s1 * (t0 * d0[IX(i1, j0)] + t1 * d0[IX(i1, j1)]);
        }
    }
    set_bnd(n, b, d);
}

void project(int n, float *u, float *v, float *p, float *div) 
{
    float h = 1.0f/n;

    for (int i = 1; i <= n; i++) {
		for (int j = 1; j <= n; j++) {
        	div[IX(i, j)] = -0.5f * h * (u[IX(i+1, j  )] - u[IX(i-1, j  )] + 
										 v[IX(i  , j+1)] - v[IX(i  , j-1)]);
        	p[IX(i, j)] = 0;
    	}
	}

    set_bnd(n, 0, div); 
	set_bnd(n, 0,   p);

    for (int k = 0; k < 20; k++) {
        for (int i = 1; i <= n; i++) {
			for (int j = 1; j <= n; j++) {
            	p[IX(i, j)] = (
						div[IX(i, j)] + p[IX(i-1, j  )] + p[IX(i+1, j  )] +
										p[IX(i  , j-1)] + p[IX(i  , j+1)])/4;
        	}
		}
        set_bnd(n, 0, p);
    }
    for (int i = 1; i <= n; i++) {
		for (int j = 1; j <= n; j++) {
        	u[IX(i, j)] -= 0.5f * (p[IX(i+1, j  )] - p[IX(i-1, j  )])/h;
        	v[IX(i, j)] -= 0.5f * (p[IX(i  , j+1)] - p[IX(i  , j-1)])/h;
    	}
	}
    set_bnd(n, 1, u); 
	set_bnd(n, 2, v);
}

void dens_step(int n, float *x, float *x0, float *u, float *v, float diff, float dt) 
{
    add_source(N, x, x0, dt);
    SWAP(x0, x); 
	diffuse(N, 0, x, x0, diff, dt);
    SWAP(x0, x); 
	advect (N, 0, x, x0, u, v, dt);
}

//vel_step (N, u, v, u_prev, v_prev, VISC, dt);
void vel_step(int n, float *u, float *v, float *u0, float *v0, float visc, float dt) 
{
    add_source(N, u, u0, dt); 
	add_source(N, v, v0, dt);

    SWAP(u0, u); 
	diffuse(N, 1, u, u0, visc, dt);
    SWAP(v0, v); 
	diffuse(N, 2, v, v0, visc, dt);

    project(N, u, v, u0, v0);

    SWAP(u0, u); 
    advect(N, 1, u, u0, u0, v0, dt); 
	SWAP(v0, v);
	advect(N, 2, v, v0, u0, v0, dt);
	
    project(N, u, v, u0, v0);
}

