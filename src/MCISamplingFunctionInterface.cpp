#include "mci/MCISamplingFunctionInterface.hpp"

#include <algorithm>

MCISamplingFunctionInterface::MCISamplingFunctionInterface(const int ndim, const int nproto):
    _ndim(ndim), _nproto(nproto)
{
    _protonew = new double[_nproto];
    _protoold = new double[_nproto];
    std::fill(_protonew, _protonew+_nproto, 0.);
    std::fill(_protoold, _protoold+_nproto, 0.);
}

MCISamplingFunctionInterface::~MCISamplingFunctionInterface()
{
    delete[] _protoold;
    delete[] _protonew;
}

void MCISamplingFunctionInterface::setNProto(const int nproto)
{
    _nproto=nproto;
    delete[] _protoold;
    delete[] _protonew;
    _protonew = new double[_nproto];
    _protoold = new double[_nproto];
    std::fill(_protonew, _protonew+_nproto, 0.);
    std::fill(_protoold, _protoold+_nproto, 0.);
}

void MCISamplingFunctionInterface::newToOld()
{   // pointer swap
    double * const foo = _protonew;
    _protonew=_protoold;
    _protoold=foo;
}