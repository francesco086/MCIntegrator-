#include "mci/AccumulatorInterface.hpp"

#include <stdexcept>

namespace mci
{

    AccumulatorInterface::AccumulatorInterface(ObservableFunctionInterface * obs, const int nskip):
        _obs(obs), _nobs(obs->getNObs()), _xndim(obs->getNDim()), _nskip(nskip),
        _nsteps(0), _stepidx(0), _skipidx(0), _flag_eval(true), _flag_final(false),
        _flags_xchanged(nullptr), _data(nullptr)
    {
        if (nskip < 1) { throw std::invalid_argument("[AccumulatorInterface] Provided number of steps per evaluation was < 1 ."); }
    }


    void AccumulatorInterface::allocate(const int nsteps)
    {
        this->deallocate(); // for safety

        if (nsteps < 1) { throw std::invalid_argument("[AccumulatorInterface::allocate] Provided number of MC steps was < 1 ."); }

        _nsteps = nsteps;
        _flags_xchanged = new bool[_xndim]; // allocate x-change flags
        std::fill(_flags_xchanged, _flags_xchanged+_xndim, true); // on the first step we need to evaluate fully

        this->_allocate(); // call child allocate
    }


    void AccumulatorInterface::accumulate(const double x[], const bool flagacc, const bool flags_xchanged[])
    {
        if (_stepidx == _nsteps) { throw std::runtime_error("[AccumulatorInterface::accumulate] Number of calls to accumulate exceed the allocation."); }

        if (flagacc) { // there was a change (so we need to evaluate obs on next skipidx==0)
            _flag_eval = true;
            for (int i=0; i<_xndim; ++i) {
                if (flags_xchanged[i]) _flags_xchanged[i] = true;
            }
        }

        if (_skipidx == 0) { // accumulate observables
            if (_flag_eval) {
                _obs->computeValues(x, _flags_xchanged);
                _flag_eval = false;
                std::fill(_flags_xchanged, _flags_xchanged+_xndim, false);
            }
            this->_accumulate(); // call child storage implementation
        }

        ++_stepidx;
        if (++_skipidx == _nskip) {
            _skipidx = 0; // next step will be evaluated
        }
    }


    void AccumulatorInterface::finalize()
    {
        if (_stepidx != _nsteps) { throw std::runtime_error("[AccumulatorInterface::finalize] Finalize was called before all steps were accumulated."); }
        if (!_flag_final) { this->_finalize(); } // call child finalize
        _flag_final = true;
    }


    void AccumulatorInterface::reset()
    {
        this->_reset(); // call child reset

        // do base reset
        _stepidx = 0;
        _skipidx = 0;
        _flag_eval = true;
        _flag_final = false;
    }


    void AccumulatorInterface::deallocate()
    {
        this->reset(); // achieve clean state
        this->_deallocate(); // call child deallocate

        delete [] _flags_xchanged;
        _flags_xchanged = nullptr;
        _nsteps = 0;
    }

}
