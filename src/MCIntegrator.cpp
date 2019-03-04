#include "mci/MCIntegrator.hpp"

#include "mci/Estimators.hpp"
#include "mci/MCIBlockAccumulator.hpp"
#include "mci/MCIFullAccumulator.hpp"
#include "mci/MCISimpleAccumulator.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>


//   --- Integrate

void MCI::integrate(const int Nmc, double * average, double * error, const bool doFindMRT2step, const bool doDecorrelation)
{
    if ( _flagpdf ) {
        //find the optimal mrt2 step
        if (doFindMRT2step) { this->findMRT2Step(); }
        // take care to do the initial decorrelation of the walker
        if (doDecorrelation) { this->initialDecorrelation(); }
    }

    // allocation of the accumulators where the data will be stored
    this->allocateObservables(Nmc);

    //sample the observables
    if (_flagobsfile) { _obsfile.open(_pathobsfile); }
    if (_flagwlkfile) { _wlkfile.open(_pathwlkfile); }
    _flagMC = true;
    this->sample(Nmc, true);
    _flagMC = false;
    if (_flagobsfile) { _obsfile.close(); }
    if (_flagwlkfile) { _wlkfile.close(); }

    // estimate average and standard deviation
    int obsidx = 0; // observable index offset
    for (auto & cont : _obsc) {
        cont.estim(average+obsidx, error+obsidx);
        obsidx += cont.accu->getNObs();
    }

    // if we sampled uniformly, scale results by volume
    if (!_flagpdf) {
        for (int i=0; i<_nobsdim; ++i) {
            average[i] *=_vol;
            error[i] *=_vol;
        }
    }

    // deallocate
    this->deallocateObservables();
}



//   --- Internal methods


void MCI::storeObservables()
{
    if ( _ridx%_freqobsfile == 0 ) {
        _obsfile << _ridx;
        for (auto & cont : _obsc) {
            MCIObservableFunctionInterface * const obs = cont.accu->getObservable(); // acquire ptr to obs
            for (int j=0; j<obs->getNObs(); ++j) {
                _obsfile << "   " << obs->getValue(j);
            }
        }
        _obsfile << std::endl;
    }
}


void MCI::storeWalkerPositions()
{
    if ( _ridx%_freqwlkfile == 0 ) {
        _wlkfile << _ridx;
        for (int j=0; j<_ndim; ++j) {
            _wlkfile << "   " << _xold[j] ;
        }
        _wlkfile << std::endl;
    }
}


void MCI::initialDecorrelation()
{
    if (_NdecorrelationSteps < 0) {
        // placeholder
        const int NMC_DECORR=1000;
        this->sample(NMC_DECORR, false);

        /* // auto decorrelation retired for the moment
        //constants
        const int MIN_NMC=100;
        //allocate the data array that will be used
        const int ndatax_tot = MIN_NMC*_nobsdim;
        datax = new double[ndatax_tot];
        //do a first estimate of the observables
        this->sample(MIN_NMC, true);
        auto * oldestimate = new double[_nobsdim];
        double olderrestim[_nobsdim];
        mci::CorrelatedEstimator(MIN_NMC, _nobsdim, _datax, oldestimate, olderrestim);

        //start a loop which will stop when the observables are stabilized
        bool flag_loop=true;
        auto * newestimate = new double[_nobsdim];
        double newerrestim[_nobsdim];
        while ( flag_loop ) {
        flag_loop = false;
        this->sample(MIN_NMC, true);
        mci::CorrelatedEstimator(MIN_NMC, _nobsdim, _datax, newestimate, newerrestim);

        for (int i=0; i<_nobsdim; ++i) {
        if ( fabs( oldestimate[i] - newestimate[i] ) > 2*sqrt( olderrestim[i]*olderrestim[i] + newerrestim[i]*newerrestim[i] ) ) {
        flag_loop=true; // if any difference is too large, continue the loop
        break; // break the inner for loop
        }
        }
        double * const foo=oldestimate;
        oldestimate=newestimate;
        newestimate=foo;
        }

        //memory deallocation
        delete [] newestimate;
        delete [] oldestimate;
        delete [] _datax;
        _datax = nullptr;
        */
    }
    else if (_NdecorrelationSteps > 0) {
        this->sample(_NdecorrelationSteps, false);
    }
}


void MCI::sample(const int npoints, const bool flagobs)
{
    // reset acceptance and rejection
    this->resetAccRejCounters();

    // reset observable data
    if (flagobs) { this->resetObservables(); }

    // first call of the call-back functions
    if (_flagpdf) {
        for (auto & cback : _cback){
            cback->callBackFunction(_xold, true);
        }
    }

    // initialize the pdf at x
    computeOldSamplingFunction();

    //start the main loop for sampling
    for (_ridx=0; _ridx<npoints; ++_ridx) {
        bool flagacc;
        if (_flagpdf) { // use sampling function
            flagacc = this->doStepMRT2();
        }
        else {
            this->newRandomX();
            ++_acc; // "accept" move
            flagacc = true;
        }

        if (flagobs) { // accumulate observables
            this->accumulateObservables(flagacc); // uses _xold
            if (_flagMC && _flagobsfile) { this->storeObservables(); } // store obs on file
        }

        if (_flagMC && _flagwlkfile) { this->storeWalkerPositions(); } // store walkers on file
    }

    // finalize data
    if (flagobs) { this->finalizeObservables(); }
}


void MCI::findMRT2Step()
{
    //constants
    const int MIN_STAT=200;  //minimum statistic: number of M(RT)^2 steps done before deciding if the step must be increased or decreased
    const int MIN_CONS=5;   //minimum consecutive: minimum number of consecutive loops without need of changing mrt2step
    const double TOLERANCE=0.05;  //tolerance: tolerance for the acceptance rate
    const int MAX_NUM_ATTEMPTS=50;  //maximum number of attempts: maximum number of time that the main loop can be executed
    const double SMALLEST_ACCEPTABLE_DOUBLE=1.e-50;

    //initialize index
    int cons_count = 0;  //number of consecutive loops without need of changing mrt2step
    int counter = 0;  //counter of loops
    double fact;
    while ( ( _NfindMRT2steps < 0 && cons_count < MIN_CONS ) || counter < _NfindMRT2steps ) {
        //do MIN_STAT M(RT)^2 steps
        this->sample(MIN_STAT,false);

        //increase or decrease mrt2step depending on the acceptance rate
        double rate = this->getAcceptanceRate();
        if ( fabs(rate-_targetaccrate) < TOLERANCE ) {
            //mrt2step was ok
            cons_count++;
        }
        else {
            //need to change mrt2step
            cons_count=0;
            fact = std::min(2., std::max(0.5, rate/_targetaccrate) );
            for (int j=0; j<_ndim; ++j) {
                _mrt2step[j] *= fact;
            }

            // sanity checks
            for (int j=0; j<_ndim; ++j) { //mrt2step = Infinity
                if ( _mrt2step[j] > ( _ubound[j] - _lbound[j] ) ) {
                    _mrt2step[j] = ( _ubound[j] - _lbound[j] );
                }
            }
            for (int j=0; j<_ndim; ++j) { //mrt2step ~ 0
                if ( _mrt2step[j] < SMALLEST_ACCEPTABLE_DOUBLE ) {
                    _mrt2step[j] = SMALLEST_ACCEPTABLE_DOUBLE;
                }
            }
        }
        counter++;

        if ( _NfindMRT2steps < 0 && counter >= MAX_NUM_ATTEMPTS ) {
            std::cout << "Warning [MCI::findMRT2Step]: Max number of attempts reached without convergence." << std::endl;
            break;
        }
    }
}


bool MCI::doStepMRT2()
{
    // propose a new position x
    this->computeNewX();

    // find the corresponding sampling function value
    this->computeNewSamplingFunction();

    //determine if the proposed x is accepted or not
    const bool flagacc = ( _rd(_rgen) <= this->computeAcceptance() );

    //update some values according to the acceptance of the mrt2 step
    if ( flagacc ) {
        //accepted
        _acc++;
        //update the walker position x
        this->updateX();
        //update the sampling function values pdfx
        this->updateSamplingFunction();
        //if there are some call back functions, invoke them
        for (MCICallBackOnAcceptanceInterface * cback : _cback){
            cback->callBackFunction(_xold, _flagMC);
        }
    } else {
        //rejected
        _rej++;
    }

    return flagacc;
}


void MCI::updateVolume()
{
    // Set the integration volume
    _vol=1.;
    for (int i=0; i<_ndim; ++i) {
        _vol = _vol*( _ubound[i] - _lbound[i] );
    }
}


void MCI::applyPBC(double * v)
{
    for (int i=0; i<_ndim; ++i) {
        while ( v[i] < _lbound[i] ) {
            v[i] += _ubound[i] - _lbound[i];
        }
        while ( v[i] > _ubound[i] ) {
            v[i] -= _ubound[i] - _lbound[i];
        }
    }
}


void MCI::updateX()
{
    double * const foo = _xold;
    _xold = _xnew;
    _xnew = foo;
}


void MCI::newRandomX()
{
    //set xold to new random values (within the irange)
    for (int i=0; i<_ndim; ++i) {
        _xold[i] = _lbound[i] + ( _ubound[i] - _lbound[i] ) * _rd(_rgen);
    }
}


void MCI::resetAccRejCounters()
{
    _acc = 0;
    _rej = 0;
}


void MCI::computeNewX()
{
    for (int i=0; i<_ndim; ++i) {
        _xnew[i] = _xold[i] + _mrt2step[i] * (_rd(_rgen)-0.5);
    }
    applyPBC(_xnew);
}


void MCI::updateSamplingFunction()
{
    for (auto & sf : _pdf) {
        sf->newToOld();
    }
}


double MCI::computeAcceptance()
{
    double acceptance=1.;
    for (auto & sf : _pdf) {
        acceptance*=sf->getAcceptance();
    }
    return acceptance;
}


void MCI::computeOldSamplingFunction()
{
    for (auto & sf : _pdf) {
        sf->computeNewSamplingFunction(_xold);
        sf->newToOld();
    }
}


void MCI::computeNewSamplingFunction()
{
    for (auto & sf : _pdf) {
        sf->computeNewSamplingFunction(_xnew);
    }
}


void MCI::allocateObservables(const int Nmc)
{   // allocate observable accumulators for Nmc steps
    for (auto & cont : _obsc) {
        cont.accu->allocate(Nmc);
    }
}

void MCI::accumulateObservables(const bool flagacc)
{   // let the accumulators process the step
    for (auto & cont : _obsc) {
        cont.accu->accumulate(_xold, flagacc);
    }
}

void MCI::finalizeObservables()
{   // apply normalization, if necessary
    for (auto & cont : _obsc) {
        cont.accu->finalize();
    }
}

void MCI::resetObservables()
{   // reset without deallocation
    for (auto & cont : _obsc) {
        cont.accu->reset();
    }
}

void MCI::deallocateObservables()
{   // reset & free memory
    for (auto & cont : _obsc) {
        cont.accu->deallocate();
    }
}


//   --- Setters


void MCI::storeObservablesOnFile(const char * filepath, const int freq)
{
    _pathobsfile.assign(filepath);
    _freqobsfile = freq;
    _flagobsfile=true;
}


void MCI::storeWalkerPositionsOnFile(const char * filepath, const int freq)
{
    _pathwlkfile.assign(filepath);
    _freqwlkfile = freq;
    _flagwlkfile=true;
}


void MCI::clearCallBackOnAcceptance(){
    _cback.clear();
}


void MCI::addCallBackOnAcceptance(MCICallBackOnAcceptanceInterface * cback){
    _cback.push_back(cback);
}


void MCI::clearObservables()
{
    for (auto & cont : _obsc) {
        delete cont.accu;
    }
    _obsc.clear();
    _nobsdim=0;
}


void MCI::addObservable(MCIObservableFunctionInterface * obs, int blocksize, int nskip, const bool flag_equil, const bool flag_correlated)
{
    // sanity
    blocksize = std::max(0, blocksize);
    nskip = std::max(1, nskip);
    if (flag_equil && blocksize==0) {
        throw std::invalid_argument("[MCI::addObservable] Requested automatic observable equilibration requires blocksize > 0.");
    }
    if (flag_equil && blocksize==0) {
        throw std::invalid_argument("[MCI::addObservable] Requested correlated error estimation requires blocksize > 0.");
    }

    // we need to select these two
    MCIAccumulatorInterface * accu;
    std::function< void(int /*nstore*/, int /*nobs*/, const double * /*data*/, double * /*avg*/, double * /*error*/) > estim;

    if (blocksize == 0) {
        accu = new MCISimpleAccumulator(obs, nskip);
        // data is already the average, so this estimator just copies the average and fills error with 0
        estim = [](int nstore /*unused*/, int nobs, const double * data, double * avg, double * err) {
                    std::copy(data, data+nobs, avg);
                    std::fill(err, err+nobs, 0.);
                };
    } else {
        if (blocksize == 1) {
            accu = new MCIFullAccumulator(obs, nskip);
        } else {
            accu = new MCIBlockAccumulator(obs, nskip, blocksize);
        }
        estim = flag_correlated ? mci::CorrelatedEstimator : mci::UncorrelatedEstimator ;
    }

    // append to vector of containers
    _obsc.emplace_back(accu, estim, flag_equil );
    _nobsdim+=accu->getNObs();
}


void MCI::clearSamplingFunctions()
{
    _pdf.clear();
    _flagpdf = false;
}


void MCI::addSamplingFunction(MCISamplingFunctionInterface * mcisf)
{
    _pdf.push_back(mcisf);
    _flagpdf = true;
}


void MCI::setTargetAcceptanceRate(const double targetaccrate)
{
    _targetaccrate = targetaccrate;
}


void MCI::setMRT2Step(const double * mrt2step)
{
    std::copy(mrt2step, mrt2step+_ndim, _mrt2step);
}


void MCI::setX(const double * x)
{
    std::copy(x, x+_ndim, _xold);
    applyPBC(_xold);
}

void MCI::setIRange(const double lbound, const double ubound)
{
    // Set irange and apply PBC to the initial walker position _x
    std::fill(_lbound, _lbound+_ndim, lbound);
    std::fill(_ubound, _ubound+_ndim, ubound);
    updateVolume();

    applyPBC(_xold);
}

void MCI::setIRange(const double * lbound, const double * ubound)
{
    // Set irange and apply PBC to the initial walker position _x
    std::copy(lbound, lbound+_ndim, _lbound);
    std::copy(ubound, ubound+_ndim, _ubound);
    updateVolume();

    applyPBC(_xold);
}

void MCI::setSeed(const uint_fast64_t seed) // fastest unsigned integer which is at least 64 bit (as expected by rgen)
{
    _rgen.seed(seed);
}


//   --- Constructor and Destructor

MCI::MCI(const int ndim)
{
    // _ndim
    _ndim = ndim;
    // _lbound and _ubound
    _lbound = new double[_ndim];
    _ubound = new double[_ndim];
    std::fill(_lbound, _lbound+_ndim, -std::numeric_limits<double>::max());
    std::fill(_ubound, _ubound+_ndim, std::numeric_limits<double>::max());
    // _vol (will only be relevant without sampling function)
    _vol=0.;

    // _x
    _xold = new double[_ndim];
    std::fill(_xold, _xold+_ndim, 0.);
    _xnew = new double[_ndim];
    std::fill(_xnew, _xnew+_ndim, 0.);
    _flagwlkfile=false;
    // _mrt2step
    _mrt2step = new double[_ndim];
    std::fill(_mrt2step, _mrt2step+_ndim, INITIAL_STEP);

    // other controls, defaulting to auto behavior
    _NfindMRT2steps = -1;
    _NdecorrelationSteps = -1;

    // probability density function
    _flagpdf = false;
    // initialize info about observables
    _nobsdim=0;
    _flagobsfile=false;
    // initialize random generator
    _rgen = std::mt19937_64(_rdev());
    _rd = std::uniform_real_distribution<double>(0.,1.);
    //initialize the running index
    _ridx=0;
    //initialize the acceptance counters
    _acc=0;
    _rej=0;
    // initialize all the other variables
    _targetaccrate=0.5;
    _flagMC=false;
}

MCI::~MCI()
{
    // clear vectors
    this->clearSamplingFunctions();
    this->clearObservables();
    this->clearCallBackOnAcceptance();

    // _mrt2step
    delete [] _mrt2step;

    // _xold and _xnew
    delete [] _xnew;
    delete [] _xold;

    // lbound and ubound
    delete [] _ubound;
    delete [] _lbound;
}
