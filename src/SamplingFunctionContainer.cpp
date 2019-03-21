#include "mci/SamplingFunctionContainer.hpp"

namespace mci
{

    void SamplingFunctionContainer::addSamplingFunction(std::unique_ptr<SamplingFunctionInterface> sf /* we acquire ownership */)
    {
        _pdfs.emplace_back(std::move(sf)); // now sf is owned by _pdfs vector
    }

    void SamplingFunctionContainer::newToOld()
    {
        for (auto & sf : _pdfs) {
            sf->newToOld();
        }
    }

    void SamplingFunctionContainer::oldToNew()
    {
        for (auto & sf : _pdfs) {
            sf->oldToNew();
        }
    }

    void SamplingFunctionContainer::initializeProtoValues(const double xold[])
    {
        for (auto & sf : _pdfs) {
            sf->initializeProtoValues(xold);
        }
    }

    double SamplingFunctionContainer::getOldSamplingFunction() const
    {
        double sampf = 1.;
        for (auto & sf : _pdfs) {
            sampf *= sf->getOldSamplingFunction();
        }
        return sampf;
    }

    double SamplingFunctionContainer::computeAcceptance(const WalkerState &wlk)
    {
        double acceptance = 1.;
        for (auto & sf : _pdfs) {
            acceptance *= sf->computeAcceptance(wlk);
        }
        return acceptance;
    }


    void SamplingFunctionContainer::clear()
    {
        _pdfs.clear();
    }

}  // namespace mci