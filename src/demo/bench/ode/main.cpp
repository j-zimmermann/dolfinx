// Copyright (C) 2005 Johan Hoffman and Anders Logg.
// Licensed under the GNU GPL Version 2.

#include <stdlib.h>
#include <dolfin.h>

using namespace dolfin;

class WaveEquation : public ODE
{
public:

  WaveEquation(unsigned int n) : ODE(2*(n+1)*(n+1)*(n+1)), 
				 n(n), offset(N/2), mesh(n, n, n),
				 ufile("solution.dx"), kfile("timesteps.dx")
  {
    T = 1.0;
    c = 1.0;

    h = 1.0 / static_cast<real>(n + 1);
    a = c*c / (h*h);
    offset = N/2;

    setSparsity();
  }

  void setSparsity()
  {
    // Dependencies for first half of system
    for (unsigned int i = 0; i < offset; i++)
    {
      sparsity.clear(i);
      sparsity.setsize(i, 1);
      sparsity.set(i, i + offset);
    }

    // Dependencies for second half of system
    for (unsigned int i = offset; i < N; i++)
    {
      const unsigned int j = i - offset;
      const unsigned int m = n + 1;
      const unsigned int jx = j % m;
      const unsigned int jy = (j / m) % m;
      const unsigned int jz = j / (m*m);

      unsigned int size = 0;
      if ( jx > 0 ) size++;
      if ( jy > 0 ) size++;
      if ( jz > 0 ) size++;
      if ( jx < n ) size++;
      if ( jy < n ) size++;
      if ( jz < n ) size++;
      sparsity.clear(i);
      sparsity.setsize(i, size);

      if ( jx > 0 ) sparsity.set(i, j - 1);
      if ( jy > 0 ) sparsity.set(i, j - m);
      if ( jz > 0 ) sparsity.set(i, j - m*m);
      if ( jx < n ) sparsity.set(i, j + 1);
      if ( jy < n ) sparsity.set(i, j + m);
      if ( jz < n ) sparsity.set(i, j + m*m);
    }
  }

  ~WaveEquation() {}

  // Initial data
  real u0(unsigned int i)
  {
    if ( i < offset )
      if ( mesh.node(i).dist(0.5, 0.5 , 0.5) < 5.0*h )
	return 1.0;
    
    return 0.0;
  }

  // Right-hand side, multi-adaptive version
  real f(const real u[], real t, unsigned int i)
  {
    // First half of system
    if ( i < offset )
      return u[i + offset];
    
    // Second half of system
    const unsigned int j = i - offset;
    const unsigned int m = n + 1;
    const unsigned int jx = j % m;
    const unsigned int jy = (j / m) % m;
    const unsigned int jz = j / (m*m);

    real sum = -6.0*u[j];
    if ( jx > 0 ) sum += u[j - 1];
    if ( jy > 0 ) sum += u[j - m];
    if ( jz > 0 ) sum += u[j - m*m];
    if ( jx < n ) sum += u[j + 1];
    if ( jy < n ) sum += u[j + m];
    if ( jz < n ) sum += u[j + m*m];

    return a*sum;
  }

  // Right-hand side, mono-adaptive version
  void f(const real u[], real t, real y[])
  {
    // First half of system
    for (unsigned int i = 0; i < offset; i++)
      y[i] = u[i + offset];

    // Second half of system
    for (unsigned int i = offset; i < N; i++)
    {
      const unsigned int j = i - offset;
      const unsigned int m = n + 1;
      const unsigned int jx = j % m;
      const unsigned int jy = (j / m) % m;
      const unsigned int jz = j / (m*m);

      real sum = -6.0*u[j];
      if ( jx > 0 ) sum += u[j - 1];
      if ( jy > 0 ) sum += u[j - m];
      if ( jz > 0 ) sum += u[j - m*m];
      if ( jx < n ) sum += u[j + 1];
      if ( jy < n ) sum += u[j + m];
      if ( jz < n ) sum += u[j + m*m];
      
      y[i] = sum;
    }
  }

  // Save solution  
  void save(NewSample& sample)
  {
    // FIXME: Don't save solution when running benchmark

    cout << "Saving data at t = " << sample.t() << endl;

    // Create vectors
    NewVector ux(N/2);
    NewVector kx(N/2);
    NewFunction u(mesh, ux);
    NewFunction k(mesh, kx);
    u.rename("u", "Solution of the wave equation");
    k.rename("k", "Time steps for the wave equation");

    // Get the degrees of freedom and set current time
    u.set(sample.t());
    k.set(sample.t());
    for (unsigned int i = 0; i < N/2; i++)
    {
      ux(i) = sample.u(i);
      kx(i) = sample.k(i);
    }

    // Save solution to file
    ufile << u;
    kfile << k;
  }

private:

  real c; // Speed of light
  real h; // Mesh size
  real a; // Product (c/h)^2

  unsigned int n;      // Number of cells in each direction
  unsigned int offset; // Offset for second half of system
  UnitCube mesh;       // The mesh
  File ufile, kfile;   // Files for saving solution

};

int main(int argc, const char* argv[])
{
  // Parse command line arguments
  if ( argc != 3 )
  {
    dolfin_info("Usage: dolfin-bench-ode method n");
    dolfin_info("");
    dolfin_info("method - 'cg', 'dg', 'mcg' or 'mdg'");
    dolfin_info("n      - number of cells in each dimension");
    return 1;
  }
  const char* method = argv[1];
  unsigned int n = static_cast<unsigned int>(atoi(argv[2]));
  if ( n < 1 )
    dolfin_error("Number of cells n must be positive.");

  // Set parameters
  dolfin_set("solve dual problem", false);
  dolfin_set("use new ode solver", true);
  dolfin_set("method", method);
  dolfin_set("fixed time step", true);

  // Solve the wave equation
  WaveEquation wave(n);
  wave.solve();
}
