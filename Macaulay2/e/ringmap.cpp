// Copyright 1996  Michael E. Stillman

#include "ringmap.hpp"
#include "matrix.hpp"
#include "matrixcon.hpp"
#include "polyring.hpp"
#include "relem.hpp"

RingMap::RingMap(const Matrix *m)
: immutable_object(0), R(m->get_ring())
{
  M = 0;
  P = R->cast_to_PolynomialRing();
  if (P != 0)
    {
      M = P->Nmonoms();
      K = P->Ncoeffs();
    }
  else 
    K = R;

  nvars = m->n_cols();
  is_monomial = true;

  ring_elem one = K->from_int(1);

  // Allocate space for the ring map elements
  _elem = new var[nvars];

  for (int i=0; i<nvars; i++)
    {
      // First initialize these fields:
      _elem[i].is_zero = false;
      _elem[i].coeff_is_one = true;
      _elem[i].monom_is_one = true;
      _elem[i].bigelem_is_one = true;
      _elem[i].coeff = ZERO_RINGELEM;
      _elem[i].monom = NULL;

      ring_elem f = m->elem(0,i);  // This does a copy.
      _elem[i].bigelem = f;

      if (R->is_zero(f))
	_elem[i].is_zero = true;
      else if (P == 0)
	{
	  // Not a polynomial ring, so put everything into coeff
	  if (!K->is_equal(f, one))
	    {
	      _elem[i].coeff_is_one = false;
	      _elem[i].coeff = K->copy(f);
	    }
	}
      else {
	// A polynomial ring.
	Nterm *t = f;
	if (t->next == NULL)
	  {
	    // This is a single term
	    if (!K->is_equal(t->coeff, one))
	      {
		_elem[i].coeff_is_one = false;
		_elem[i].coeff = K->copy(t->coeff);
	      }
	    
	    if (!M->is_one(t->monom))  // should handle M->n_vars() == 0 case correctly.
	      {
		_elem[i].monom_is_one = false;
		_elem[i].monom = M->make_new(t->monom);
	      }
	  }
	else
	  {
	    // This is a bigterm
	    is_monomial = false;
	    _elem[i].bigelem_is_one = false;
	  }
      }
      K->remove(one);
    }
}

RingMap::~RingMap()
{
  for (int i=0; i<nvars; i++)
    {
      if (!_elem[i].coeff_is_one) K->remove(_elem[i].coeff);
      if (!_elem[i].monom_is_one) M->remove(_elem[i].monom);
      R->remove(_elem[i].bigelem);
    }
  deletearray(_elem);
  K = NULL;
  M = NULL;
}

bool RingMap::is_equal(const RingMap *phi) const
{
  // Two ringmap's are identical if their 'bigelem's are the same
  if (R != phi->get_ring()) return false;
  if (nvars != phi->nvars) return false;

  for (int i=0; i<nvars; i++)
    if (!R->is_equal(elem(i), phi->elem(i)))
      return false;

  return true;
}

const RingMap *RingMap::make(const Matrix *m)
{
  RingMap *result = new RingMap(m);
  // MES: set hash value
  return result;
}

void RingMap::text_out(buffer &o) const
{
  o << "(";
  for (int i=0; i<nvars; i++)
    {
      if (i > 0) o << ", ";
      R->elem_text_out(o, _elem[i].bigelem);
    }
  o << ")";
}

ring_elem RingMap::eval_term(const Ring *sourceK,
			      const ring_elem a, const int *vp) const
{
  int i;
  assert(sourceK->total_n_vars() <= nvars);
  int first_var = sourceK->total_n_vars();
  int npairs = *vp++ - 1;
  for (i=0; i<npairs; i++)
    {
      int v = first_var + varpower::var(vp[i]);
      if (v >= nvars || _elem[v].is_zero)
	return R->from_int(0);	// The result is zero.
    }

  // If K is a coeff ring of R, AND map is an identity on K,
  // then don't recurse: use this value directly.
  // Otherwise, we must recurse, I guess.
  ring_elem result = sourceK->eval(this, a);
  if (R->is_zero(result)) return result;

  int *result_monom;
  int *temp_monom;
  ring_elem result_coeff = K->from_int(1);

  if (P != 0)
    {
      result_monom = M->make_one();
      temp_monom = M->make_one();
    }

  if (!R->is_commutative_ring())
    {
      // This is the only non-commutative case so far
      for (i=0; i<npairs; i++)
	{
	  int v = first_var + varpower::var(vp[i]);
	  int e = varpower::exponent(vp[i]);
	  for (int j=0; j<e; j++)
	    {
	      assert(v < nvars);
	      ring_elem tmp = R->mult(_elem[v].bigelem,result);
	      R->remove(result);
	      result = tmp;
	    }
	}
    }
  else {
    for (i=0; i<npairs; i++)
      {
	int v = first_var + varpower::var(vp[i]);
	int e = varpower::exponent(vp[i]);
	assert(v < nvars);
	if (_elem[v].bigelem_is_one)
	  {
	    if (!_elem[v].coeff_is_one)
	      {
		ring_elem tmp = K->power(_elem[v].coeff, e);
		K->mult_to(result_coeff, tmp);
		K->remove(tmp);
	      }
	    if (!_elem[v].monom_is_one)
	      {
		M->power(_elem[v].monom, e, temp_monom);
		M->mult(result_monom, temp_monom, result_monom);
	      }
	  }
	else
	  {
	    ring_elem thispart = R->power(_elem[v].bigelem, e);
	    R->mult_to(result, thispart);
	    R->remove(thispart);
	    if (R->is_zero(result)) break;
	  }
      }
    if (P != 0)
      {
	ring_elem temp = P->term(result_coeff, result_monom);
	K->remove(result_coeff);
	M->remove(result_monom);
	M->remove(temp_monom);
	P->mult_to(result,temp);
	P->remove(temp);
      }
  }
  return result;
}

RingElementOrNull *RingMap::eval(const RingElement *r) const
{
  RingElement *result = RingElement::make_raw(get_ring(), r->get_ring()->eval(this, r->get_value()));
  if (error()) return 0;
  return result;
}

MatrixOrNull *RingMap::eval(const FreeModule *F, const Matrix *m) const
{
  MatrixConstructor mat(F, 0, false);
  for (int i=0; i<m->n_cols(); i++)
    mat.append(m->rows()->eval(this, F, (*m)[i]));
  if (error()) return 0;
  return mat.to_matrix();
}



// Local Variables:
// compile-command: "make -C $M2BUILDDIR/Macaulay2/e "
// End:
