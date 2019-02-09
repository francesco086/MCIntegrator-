#include "mci/MCIntegrator.hpp"
#include "mci/MCISamplingFunctionInterface.hpp"

#include <iostream>
#include <math.h>
#include <assert.h>

using namespace std;



class ThreeDimGaussianPDF: public MCISamplingFunctionInterface{
public:
    ThreeDimGaussianPDF(): MCISamplingFunctionInterface(3, 1){}
    ~ThreeDimGaussianPDF(){}

    void samplingFunction(const double *in, double * protovalues){
        protovalues[0] = (in[0]*in[0]) + (in[1]*in[1]) + (in[2]*in[2]);
    }


    double getAcceptance(const double * protoold, const double * protonew){
        return exp(-protonew[0]+protoold[0]);
    }

};


class XSquared: public MCIObservableFunctionInterface{
public:
    XSquared(): MCIObservableFunctionInterface(3, 1){}
    ~XSquared(){}

    void observableFunction(const double * in, double * out){
        out[0] = in[0] * in[0];
    }
};



int main(){
    const long NMC = 10000;
    const double CORRECT_RESULT = 0.5;

    ThreeDimGaussianPDF * pdf = new ThreeDimGaussianPDF();
    XSquared * obs = new XSquared();

    MCI * mci = new MCI(3);
    mci->setSeed(5649871);
    mci->addSamplingFunction(pdf);
    mci->addObservable(obs);
    // the integral should provide 0.5 as answer!

    double * x = new double[3];
    x[0] = 5.; x[1] = -5.; x[2] = 10.;

    double * average = new double;
    double * error = new double;

    // this integral will give a wrong answer! This is because the starting point is very bad and initialDecorrelation is skipped (as well as the MRT2step automatic setting)
    mci->setX(x);
    mci->integrate(NMC, average, error, false, false);
    assert( abs(average[0]-CORRECT_RESULT) > 2.*error[0] );

    // this integral, instead, will provide the right answer
    mci->setX(x);
    mci->integrate(NMC, average, error);
    assert( abs(average[0]-CORRECT_RESULT) < 2.*error[0] );

    // now, doing an integral without finding again the MRT2step and doing the initialDecorrelation will also result in a correct result
    mci->integrate(NMC, average, error, false, false);
    assert( abs(average[0]-CORRECT_RESULT) < 2.*error[0] );



    delete pdf;
    delete obs;
    delete mci;
    delete [] x;
    delete average;
    delete error;

    return 0;
}