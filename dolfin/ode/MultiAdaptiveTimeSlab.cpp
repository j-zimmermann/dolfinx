// Copyright (C) 2005-2008 Anders Logg.
// Licensed under the GNU LGPL Version 2.1.
//
// Modified by Garth N. Wells
//
// First added:  2005-01-27
// Last changed: 2008-10-07

#include <string>
#include <algorithm>
#include <dolfin/common/constants.h>
#include <dolfin/log/dolfin_log.h>
#include <dolfin/parameter/parameters.h>
#include "ODE.h"
#include "Dependencies.h"
#include "Method.h"
#include "MultiAdaptiveFixedPointSolver.h"
#include "MultiAdaptiveNewtonSolver.h"
#include "MultiAdaptiveTimeSlab.h"

using namespace dolfin;

//-----------------------------------------------------------------------------
MultiAdaptiveTimeSlab::MultiAdaptiveTimeSlab(ODE& ode) : 
  TimeSlab(ode),
  sa(0), sb(0), ei(0), es(0), ee(0), ed(0), jx(0), de(0),
  ns(0), ne(0), nj(0), nd(0), solver(0), adaptivity(ode, *method), partition(N),
  elast(0), f0(0), u(0), emax(0), kmin(0)
{
  // Choose solver
  solver = chooseSolver();

  // Initialize elast
  elast = new int[N];
  for (uint i = 0; i < N; i++)
    elast[i] = -1;

  // Initialize f at left end-point for cG
  if ( method->type() == Method::cG )
    f0 = new real[N];

  // Initialize vector for u
  u = new real[N];
  real_zero(N, u);

  // Initialize transpose of dependencies if necessary
  message("Computing transpose (inverse) of dependency pattern.");
  if ( ode.dependencies.sparse() && !ode.transpose.sparse() )
    ode.transpose.transp(ode.dependencies);
}
//-----------------------------------------------------------------------------
MultiAdaptiveTimeSlab::~MultiAdaptiveTimeSlab()
{
  delete [] sa;
  delete [] sb;
  delete [] ei;
  delete [] es;
  delete [] ee;
  delete [] ed;
  delete [] jx;
  delete [] de;

  delete solver;

  delete [] elast;
  delete [] f0;
  delete [] u;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::build(real a, real b)
{
  //cout << "Multi-adaptive time slab: building between "
  //     << a << " and " << b << endl;
  
  // Allocate data
  allocData(a, b);

  // Reset elast
  for (uint i = 0; i < N; i++)
    elast[i] = -1;

  // Create time slab recursively
  kmin = ode.endtime();
  b = createTimeSlab(a, b, 0);

  //cout << "de = "; Alloc::disp(de, nd);
  
  // Save start and end time
  _a = a;
  _b = b;

  //cout << "Multi-adaptive time slab: finished building between "
  //     << a << " and " << b << ": K = " << b - a << ", nj = " << nj << endl;

  // Update at t = 0.0
  if ( a < real_epsilon() )
    ode.update(u0, a, false);

  return b;
}
//-----------------------------------------------------------------------------
bool MultiAdaptiveTimeSlab::solve()
{
  //message("Solving time slab system on [%f, %f].", _a, _b);

  // Copy u0 to u. This happens automatically in feval if user has set
  // dependencies correctly, but you never know...
  for (unsigned int i = 0; i < N; i++)
    u[i] = u0[i];

  // Compute f at left end-point for cG
  if ( method->type() == Method::cG )
  {
    for (uint i = 0; i < N; i++)
      f0[i] = ode.f(u0, _a, i);
  }

  // Solve system
  return solver->solve();

  //for (uint i = 0; i < N; i++)
  // {
  //  real endval = jx[elast[i] * method->nsize() + method->nsize() - 1];
  //  message("i = %d: u = %.16e", i, endval);
  // }
}
//-----------------------------------------------------------------------------
bool MultiAdaptiveTimeSlab::check(bool first)
{
  // Compute new time steps
  adaptivity.update(*this, _b, first);

  // Check if current solution can be accepted
  return adaptivity.accept();
}
//-----------------------------------------------------------------------------
bool MultiAdaptiveTimeSlab::shift(bool end)
{
  // Cover end time
  coverTime(_b);

  // Update the solution vector at the end time for each component
  for (uint i = 0; i < N; i++)
  {
    // Get last element of component
    const int e = elast[i];
    dolfin_assert(e != -1);
    dolfin_assert(sb[es[e]] == _b);
    
    // Get end-time value of component
    const int j = e * method->nsize();
    u[i] = jx[j + method->nsize() - 1];
  }

  // Write solution at final time if we should
  if ( save_final && end )
    write(N, u);

  // Let user update ODE
  if ( !ode.update(u, _b, end) )
    return false;

  // Set initial value to end-time value
  for (uint i = 0; i < N; i++)
    u0[i] = u[i];

  return true;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::reset()
{
  // Iterate over all elements
  uint j = 0;
  for (uint e = 0; e < ne; e++)
  {
    // Get component index
    const uint i = ei[e];

    // Iterate over degrees of freedom on element
    for (uint n = 0; n < method->nsize(); n++)
      jx[j + n] = u0[i];

    // Step to next element
    j += method->nsize();
  }
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::sample(real t)
{
  // Cover the given time
  coverTime(t);

  //cout << "t = " << t << " elast: ";
  //for (uint i = 0; i < N; i++)
  //  cout << elast[i] << " ";
  //cout << endl;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::usample(uint i, real t)
{
  // Get element
  const int e = elast[i];
  dolfin_assert(e != -1);

  // Get element data
  const uint s = es[e];
  const uint j = e * method->nsize();
  const real a = sa[s];
  const real b = sb[s];
  const real k = b - a;

  // Get initial value for element (only necessary for cG)
  const int ep = ee[e];
  const uint jp = ep * method->nsize();
  const real x0 = ( ep != -1 ? jx[jp + method->nsize() - 1] : u0[i] );
  
  // Evaluate solution
  const real tau = (t - a) / k;
  const real value = method->ueval(x0, jx + j, tau);

  return value;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::ksample(uint i, real t)
{
  // Get element
  const int e = elast[i];
  dolfin_assert(e != -1);

  // Get element data
  const uint s = es[e];
  const real a = sa[s];
  const real b = sb[s];

  // Compute time step
  const real k = b - a;

  return k;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::rsample(uint i, real t)
{
  /*
  // Note that the residual is always sampled at the end-time
  
  // Cover end time
  coverTime(_b);
  
  // Update the solution vector at the end time for each dependent component
  
  // Get list of dependencies for component
  const std::vector<uint>& deps = ode.dependencies[i];
  
  // Iterate over dependencies
  for (uint pos = 0; pos < deps.size(); pos++)
  {
    // Get last element of component
    const int e = elast[pos];
    dolfin_assert(e != -1);
    dolfin_assert(sb[es[e]] == _b);
    
    // Get end-time value of component
    const int j = e * method->nsize();
    u[pos] = jx[j + method->nsize() - 1];
  }
  
  // Compute residual
  
  // Get last element of component
  const int e = elast[i];
  dolfin_assert(e != -1);
  
  // Get element data
  const uint s = es[e];
  const uint j = e * method->nsize();
  const real a = sa[s];
  const real b = sb[s];
  const real k = b - a;
  
  // Get initial value for element (only necessary for cG)
  const int ep = ee[e];
  const uint jp = ep * method->nsize();
  const real x0 = ( ep != -1 ? jx[jp + method->nsize() - 1] : u0(i) );
  
  // Evaluate right-hand side at end-point (u is already updated)
  const real f = ode.f(u, b, i);
  
  // Compute residual
  const real r = method->residual(x0, jx + j, f, k);
  */

  // Just return previously computed maximum in time slab for component
  return adaptivity.residual(i);
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::disp() const
{
  cout << "--------------------------------------------------------" << endl;

  message("s: size = %d alloc = %d", ns, size_s.size);
  message("e: size = %d alloc = %d", ne, size_e.size);
  message("j: size = %d alloc = %d", nj, size_j.size);
  message("d: size = %d alloc = %d", nd, size_d.size);

  cout << "sa = "; Alloc::disp(sa, ns);
  cout << "sb = "; Alloc::disp(sb, ns);
 
  cout << endl;

  cout << "ei = "; Alloc::disp(ei, ne);
  cout << "es = "; Alloc::disp(es, ne);  
  cout << "ee = "; Alloc::disp(ee, ne);
  cout << "ed = "; Alloc::disp(ed, ne);

  cout << endl;

  cout << "jx = "; Alloc::disp(jx, nj);

  cout << endl;

  cout << "de = "; Alloc::disp(de, nd);
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::allocData(real a, real b)
{ 
  // Use u to keep track of the latest time value for each component here
  for (uint i = 0; i < N; i++)
    u[i] = a;
  
  // Recursively compute data size
  ns = ne = nj = nd = 0;
  computeDataSize(a, b, 0);

  // Allocate data
  alloc_s(ns);
  alloc_e(ne);
  alloc_j(nj);
  alloc_d(nd);

  // Reset mapping de
  for (uint d = 0; d < nd; d++)
    de[d] = -1;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::createTimeSlab(real a, real b, uint offset)
{
  // Compute end time of this sub slab
  uint end = 0;
  b = computeEndTime(a, b, offset, end);

  //cout << "Creating sub slab between a = " << a << " and b = " << b << endl;

  // Create sub slab
  create_s(a, b, offset, end);

  // Recursively create sub slabs for components with small time steps
  real t = a;
  while ( t < b && end < partition.size() )
    t = createTimeSlab(t, b, end);
  
  return b;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::create_s(real a0, real b0, uint offset, uint end)
{
  dolfin_assert(size_s.next < size_s.size);
  
  // Get next available position
  uint pos = size_s.next++;

  // Create new sub slab
  sa[pos] = a0;
  sb[pos] = b0;

  // Create elements for sub slab
  for (uint n = offset; n < end; n++)
    create_e(partition.index(n), pos, a0, b0);

  // Create mapping ed
  for (uint n = offset; n < end; n++)
  {
    const uint index = partition.index(n);
    const int element = elast[index];
    dolfin_assert(element != -1);

    // Count number of dependencies from element
    size_d.next += countDependencies(index, b0);

    // Update mapping ed
    if ( element == 0 )
      ed[element] = 0;
    if ( element < static_cast<int>(ne - 1) )
      ed[element + 1] = size_d.next;
  }
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::create_e(uint index, uint subslab, real a, real b)
{
  dolfin_assert(size_e.next < size_e.size);
  
  // Get next available position
  uint pos = size_e.next++;

  //message("  Creating element e = %d for i = %d at [%f, %f]", pos, index, a, b);
  
  //if ( index == 145 )
  //  cout << "Modified: " << b - a << endl << endl;

  // Create new element
  ei[pos] = index;
  es[pos] = subslab;
  ee[pos] = elast[index];

  // Create dofs for element
  create_j(index);

  // Create dependencies to element
  create_d(index, pos, subslab, a, b);

  // Update elast for component
  elast[index] = pos;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::create_j(uint index)
{
  dolfin_assert((size_j.next + method->nsize() - 1) < size_j.size);

  // Get next available position
  uint pos = size_j.next;
  size_j.next += method->nsize();

  // Create dofs
  for (uint n = 0; n < method->nsize(); n++)
    jx[pos + n] = u0[index];
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::create_d(uint i0, uint e0, uint s0, real a0, real b0)
{
  // Add dependencies to elements that depend on the given element if the
  // depending elements use larger time steps
  
  //message("Checking dependencies to element %d (component %d)", element, index);

  // Get list of components depending on current component
  const std::vector<uint>& deps = ode.transpose[i0];
  
  // Iterate over dependencies
  for (uint pos = 0; pos < deps.size(); pos++)
  {
    // Get component index of other component
    const uint i1 = deps[pos];
        
    // Get other element
    const int e1 = elast[i1];
    
    //cout << "  Other element: " << e1 << endl;
    
    // Skip elements which have not been created (use smaller time steps)
    if ( e1 == -1 )
      continue;
    
    // Get data of other element
    const uint s1 = es[e1];
    const real a1 = sa[s1];
    const real b1 = sb[s1];
    const real k1 = b1 - a1;
    
    // Only add dependencies from components with larger time steps
    if ( !within(a0, b0, a1, b1) || s0 == s1 )
      continue;
    
    //message("  Checking element %d (component %d)", e1, i1);
    
    // Iterate over dofs for element
    for (uint n = 0; n < method->nsize(); n++)
    {
      //const uint j = j1 + n;
      const real t = a1 + k1*method->npoint(n);
      
      //message("    Checking dof at t = %f", t);
      
      // Check if dof is contained in the current element
      if ( within(t, a0, b0) )
      {
	// Search for an empty position
	bool found = false;
	
	//cout << "    --- Creating dependency to element = " << element << endl;
	//cout << "    --- Starting at ed = " << ed[e1] << endl;
	//cout << "    de = "; Alloc::disp(ed, ne);
	//cout << "    nd = "; Alloc::disp(de, nd);	

	for (uint d = ed[e1]; d < ed[e1 + 1]; d++)
	{
	  if ( de[d] == -1 )
	  {
	    de[d] = e0;
	    found = true;
	    break;
	  }
	}
	dolfin_assert(found);
      }
    }
  }
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::alloc_s(uint newsize)
{
  size_s.next = 0;

  if ( newsize <= size_s.size ) return;

  //message("Reallocating: ns = %d", newsize);

  Alloc::realloc(&sa, size_s.size, newsize);
  Alloc::realloc(&sb, size_s.size, newsize);

  size_s.size = newsize;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::alloc_e(uint newsize)
{
  size_e.next = 0;

  if ( newsize <= size_e.size ) return;

  //message("Reallocating: ne = %d", newsize);

  Alloc::realloc(&ei, size_e.size, newsize);
  Alloc::realloc(&es, size_e.size, newsize);
  Alloc::realloc(&ee, size_e.size, newsize);
  Alloc::realloc(&ed, size_e.size, newsize);

  size_e.size = newsize;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::alloc_j(uint newsize)
{
  size_j.next = 0;

  if ( newsize <= size_j.size ) return;

  //message("Reallocating: nj = %d", newsize);

  Alloc::realloc(&jx, size_j.size, newsize);

  size_j.size = newsize;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::alloc_d(uint newsize)
{
  size_d.next = 0;

  if ( newsize <= size_d.size ) return;

  //message("Reallocating: nd = %d", newsize);

  Alloc::realloc(&de, size_d.size, newsize);

  size_d.size = newsize;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::computeEndTime(real a, real b, uint offset, uint& end)
{
  // Update partitition 
  real K = min(adaptivity.kmax(), b - a);
  K = partition.update(offset, end, adaptivity, K);

  //partition.debug(offset, end);
  
  // Modify time step if we're close to the end time
  if ( K < adaptivity.threshold() * (b - a) )
    b = a + K;

  // Save minimum time step
  kmin = min(kmin, b - a);
  
  return b;
}
//-----------------------------------------------------------------------------
real MultiAdaptiveTimeSlab::computeDataSize(real a, real b, uint offset)
{
  // Recursively compute data sizes using the same algorithm as
  // for the recursive creation of the time slab

  // Compute end time of this sub slab
  uint end = 0;
  b = computeEndTime(a, b, offset, end);

  // Use u to keep track of the latest time value for each component here
  for (uint n = offset; n < end; n++)
    u[partition.index(n)] = b;

  // Add contribution from this sub slab
  ns += 1;
  ne += end - offset;
  nj += method->nsize()*(end - offset);
  for (uint n = offset; n < end; n++)
    nd += countDependencies(partition.index(n));

  // Add contribution from all sub slabs
  real t = a;
  while ( t < b && end < partition.size() )
    t = computeDataSize(t, b, end);

  return b;
}
//-----------------------------------------------------------------------------
dolfin::uint MultiAdaptiveTimeSlab::countDependencies(uint i0)
{
  // Count the number of dependencies to components with smaller time steps
  // for the given component. This version is used before any elements are
  // created when we recursively compute the data size. We then use the
  // array u to store the latest time value for each component.

  uint n = 0;

  // Get list of dependencies for current component index
  const std::vector<uint>& deps = ode.dependencies[i0];
  
  // Iterate over dependencies
  for (uint pos = 0; pos < deps.size(); pos++)
  {
    // Get index of other component
    const uint i1 = deps[pos];
    
    // Use u to keep track of the latest time value for each component here
    if ( u[i0] > (u[i1] + real_epsilon()) )
      n += method->nsize();
  }
  
  return n;
}
//-----------------------------------------------------------------------------
dolfin::uint MultiAdaptiveTimeSlab::countDependencies(uint i0, real b0)
{
  // Count the number of dependencies to components with smaller time steps
  // for the given component. This version is used at the time of creation
  // of elements and we may then get the time values of already created
  // elements.

  uint n = 0;

  // Get list of dependencies for current component index
  const std::vector<uint>& deps = ode.dependencies[i0];
  
  // Iterate over dependencies
  for (uint pos = 0; pos < deps.size(); pos++)
  {
    // Get index of other component
    const uint i1 = deps[pos];
    
    // Get last element for component
    const int e1 = elast[i1];
    
    // If we have not yet created the element, then it has not reached b0
    if ( e1 == -1 )
    {
      n += method->nsize();
      continue;
    }
    
    // Need to check end time value of element
    const uint s1 = es[e1];
    const real b1 = sb[s1];
    
    // Check if the component has reached b0
    if ( b1 < (b0 - real_epsilon()) )
    {
      n += method->nsize();
    }
  }

  return n;
}
//-----------------------------------------------------------------------------
bool MultiAdaptiveTimeSlab::within(real t, real a, real b) const
{
  // Check if time is within the given interval, choosing the left interval
  // if we are close to the edge

  return (a + real_epsilon()) < t && t <= (b + real_epsilon());
}
//-----------------------------------------------------------------------------
bool MultiAdaptiveTimeSlab::within(real a0, real b0, real a1, real b1) const
{
  // Check if [a0, b0] is contained in [a1, b1]

  return a1 <= (a0 + real_epsilon()) && (b0 - real_epsilon()) <= b1;
}
//-----------------------------------------------------------------------------
dolfin::uint MultiAdaptiveTimeSlab::coverSlab(int subslab, uint e0)
{
  // Start at e0 and step until we reach a new sub slab
  uint e = e0;
  for (; e < ne; e++)
  {
    // Check if we have reached the next sub slab
    if ( static_cast<int>(es[e]) != subslab )
      break;
    
    // Update elast 
    elast[ei[e]] = e;
  }

  // Return e1
  return e;
}
//-----------------------------------------------------------------------------
dolfin::uint MultiAdaptiveTimeSlab::coverNext(int subslab, uint element)
{
  // Check if we are still on the same sub slab
  if ( subslab == static_cast<int>(es[element]) )
    return subslab;

  // Get next sub slab
  subslab = es[element];
  
  // Update elast for all elements in the sub slab
  for (uint e = element; e < ne; e++)
  {
    // Check if we have reached the next sub slab
    if ( static_cast<int>(es[e]) != subslab )
      break;

    // Update elast 
    elast[ei[e]] = e;
  }

  return subslab;
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::coverTime(real t)
{
  // Check if t is covered for all components
  bool ok = true;
  for (uint i = 0; i < N; i++)
  {
    // Get last covered element for the component
    const int e = elast[i];

    // Check if we need to start from the beginning
    if ( e == -1 )
    {
      emax = 0;
      ok = false;
      break;
    }

    // Get element data
    const uint s = es[e];
    const real a = sa[s];
    const real b = sb[s];

    // Check if we need to start from the beginning
    if ( t < (a + real_epsilon()) )
    {
      emax = 0;
      ok = false;
      break;
    }

    // Check if we need to search forward, starting at e = emax
    if ( t > (b + real_epsilon()) )
    {
      ok = false;
      break;
    }
  }

  // If ok is true, then a + real_epsilon() <= t <= b + real_epsilon() for all components
  if ( ok )
    return;

  // Reset sampling if necessary
  if ( emax >= ne )
    emax = 0;
  else
  {
    const uint s = es[emax];
    const real a = sa[s];
    
    if ( t < (a + real_epsilon()) )
      emax = 0;
  }

  // Iterate forward until t is covered for all components
  for (uint e = emax; e < ne; e++)
  {
    // Get element data
    const uint s = es[e];
    const uint i = ei[e];
    const real a = sa[s];

    // Check if we have stepped far enough
    if ( t < (a + real_epsilon()) && _a < (a - real_epsilon()) )
      break;

    // Cover element
    elast[i] = e;
    emax = e;
  }
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::cg_feval(real* f, uint s0, uint e0, uint i0, 
				    real a0, real b0, real k0)
{
  const uint& nn = method->nsize();
  const uint last = nn - 1;

  // Get list of dependencies for given component index
  const std::vector<uint>& deps = ode.dependencies[i0];

  // First evaluate at left end-point
  if ( a0 < (_a + real_epsilon()) )
  {
    // Use previously computed value
    f[0] = f0[i0];
  }
  else
  {
    // Iterate over dependencies
    for (uint pos = 0; pos < deps.size(); pos++)
    {
      // Get other element
      const uint i1 = deps[pos];
      const int e1 = elast[i1];
      
      // Special case, component has no latest element
      if ( e1 == -1 )
      {
	u[i1] = u0[i1];
	continue;
      }
      
      // Three cases: k1 = k0, k1 < k0, k1 > k0
      const uint s1 = es[e1];
      if ( s1 == s0 )
      {
	// k1 = k0 (same sub slab)
	const int ep = ee[e1];
	const uint jp = ep * nn;
	u[i1] = ( ep != -1 ? jx[jp + last] : u0[i1] );
      }
      else
      {
	const real b1 = sb[s1];
	if ( b1 < (a0 + real_epsilon()) )
	{
	  // k1 < k0 (smaller time step)
	  u[i1] = jx[e1 * nn + last];
	}
	else
	{
	  // k1 > k0 (larger time step)
	  const real a1 = sa[s1];
	  const real k1 = b1 - a1;
	  const real tau = (a0 - a1) / k1;
	  const int ep = ee[e1];
	  const uint jp = ep * nn;
	  const uint j1 = e1 * nn;
	  const real x0 = ( ep != -1 ? jx[jp + last] : u0[i1] );
	  u[i1] = method->ueval(x0, jx + j1, tau);
	}
      }
    }
    
    // Evaluate right-hand side
    f[0] = ode.f(u, a0, i0);
  }

  // Get first dependency to components with smaller time steps for element
  uint d = ed[e0];

  // Compute number of such dependencies for each nodal point
  const uint end = ( e0 < (ne - 1) ? ed[e0 + 1] : nd );
  const uint ndep = (end - d) / nn;
  dolfin_assert(ndep * nn == (end - d));

  // Evaluate the right-hand side at all quadrature points but the first
  for (uint m = 1; m < method->qsize(); m++)
  {
    // Compute quadrature point
    const real t = a0 + k0*method->qpoint(m);

    // Update values for components with larger or equal time steps
    for (uint pos = 0; pos < deps.size(); pos++)
    {
      // Get other element
      const uint i1 = deps[pos];
      const int e1 = elast[i1];
      
      // Special case, component has no latest element
      if ( e1 == -1 )
	continue;
      
      // Use fast evaluation for elements in the same sub slab
      const uint s1 = es[e1];
      const uint j1 = e1 * nn;
      if ( s0 == s1 )
      {
	u[i1] = jx[j1 + m - 1];
	continue;
      }

      // Skip components with smaller time steps
      const real b1 = sb[s1];
      if ( b1 < (a0 + real_epsilon()) )
       	continue;
      
      // Interpolate value from larger element
      const real a1 = sa[s1];
      const real k1 = b1 - a1;
      const real tau = (t - a1) / k1;
      const int ep = ee[e1];
      const uint jp = ep * nn;
      const real x0 = ( ep != -1 ? jx[jp + last] : u0[i1] );
      u[i1] = method->ueval(x0, jx + j1, tau);
    }

    // Update values for components with smaller time steps
    for (uint dep = 0; dep < ndep; dep++)
    {
      // Get element
      const int e1 = de[d++];
      dolfin_assert(e1 != -1);

      // Get initial value for element
      const int ep = ee[e1];
      const uint i1 = ei[e1];
      const uint jp = ep * nn;
      const real x0 = ( ep != -1 ? jx[jp + last] : u0[i1] );
      
      // Interpolate value from smaller element
      const uint s1 = es[e1];
      const real a1 = sa[s1];
      const real b1 = sb[s1];
      const real k1 = b1 - a1;
      const real tau = (t - a1) / k1;
      const uint j1 = e1 * nn;
      u[i1] = method->ueval(x0, jx + j1, tau);
    }
    
    // Evaluate right-hand side
    f[m] = ode.f(u, t, i0);
  }
}
//-----------------------------------------------------------------------------
void MultiAdaptiveTimeSlab::dg_feval(real* f, uint s0, uint e0, uint i0, 
				  real a0, real b0, real k0)
{
  const uint& nn = method->nsize();

  // Get list of dependencies for given component index
  const std::vector<uint>& deps = ode.dependencies[i0];

  // Get first dependency to components with smaller time steps for element
  uint d = ed[e0];

  // Compute number of such dependencies for each nodal point
  const uint end = ( e0 < (ne - 1) ? ed[e0 + 1] : nd );
  const uint ndep = (end - d) / nn;
  dolfin_assert(ndep * nn == (end - d));

  // Evaluate the right-hand side at all quadrature points
  for (uint m = 0; m < method->qsize(); m++)
  {
    // Compute quadrature point
    const real t = a0 + k0*method->qpoint(m);

    // Update values for components with larger or equal time steps
    for (uint pos = 0; pos < deps.size(); pos++)
    {
      // Get other element
      const uint i1 = deps[pos];
      const int e1 = elast[i1];
      
      // Special case, component has no latest element
      if ( e1 == -1 )
	continue;
      
      // Use fast evaluation for elements in the same sub slab
      const uint s1 = es[e1];
      const uint j1 = e1 * nn;
      if ( s0 == s1 )
      {
	u[i1] = jx[j1 + m];
	continue;
      }

      // Skip components with smaller time steps
      const real b1 = sb[s1];
      if ( b1 < (a0 + real_epsilon()) )
       	continue;
      
      // Interpolate value from larger element
      const real a1 = sa[s1];
      const real k1 = b1 - a1;
      const real tau = (t - a1) / k1;
      u[i1] = method->ueval(0.0, jx + j1, tau);
    }

    // Update values for components with smaller time steps
    for (uint dep = 0; dep < ndep; dep++)
    {
      // Get element
      const int e1 = de[d++];
      dolfin_assert(e1 != -1);

      // Interpolate value from smaller element
      const uint i1 = ei[e1];
      const uint s1 = es[e1];
      const real a1 = sa[s1];
      const real b1 = sb[s1];
      const real k1 = b1 - a1;
      const real tau = (t - a1) / k1;
      const uint j1 = e1 * nn;
      u[i1] = method->ueval(0.0, jx + j1, tau);
    }
    
    // Evaluate right-hand side
    f[m] = ode.f(u, t, i0);
  }
}
//-----------------------------------------------------------------------------
TimeSlabSolver* MultiAdaptiveTimeSlab::chooseSolver()
{
  bool implicit = ode.get("ODE implicit");
  std::string solver = ode.get("ODE nonlinear solver");

  if ( implicit )
    error("Multi-adaptive solver cannot solver implicit ODEs. Use cG(q) or dG(q) instead.");

  if ( solver == "fixed-point" )
  {
    message("Using multi-adaptive fixed-point solver.");
    return new MultiAdaptiveFixedPointSolver(*this);
  }
  else if ( solver == "newton" )
  {
    message("Using multi-adaptive Newton solver.");
    return new MultiAdaptiveNewtonSolver(*this);
  }
  else if ( solver == "default" )
  {
    message("Using multi-adaptive fixed-point solver (default for mc/dG(q)).");
    return new MultiAdaptiveFixedPointSolver(*this);
  }
  else
  {
    error("Uknown solver type: %s.", solver.c_str());
  }

  return 0;
}
//------------------------------------------------------------------------
