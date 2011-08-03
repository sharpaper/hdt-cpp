#include <iostream>
#include <fstream>

#include <inttypes.h>

#include <HDTFactory.hpp>
#include <HDTVocabulary.hpp>

#include <QtGui>

#include "hdtmanager.hpp"
#include "hdtoperation.hpp"
#include "hdtcachedinfo.hpp"
#include "stringutils.hpp"

HDTManager::HDTManager(QObject *parent) :
    QObject(parent),
    hdt(NULL),
    searchPatternID(0,0,0),
    numResults(0),
    iteratorResults(NULL)
{
    subjectModel = new TripleComponentModel(this, this, hdt::SUBJECT);
    predicateModel = new TripleComponentModel(this, this, hdt::PREDICATE);
    objectModel = new TripleComponentModel(this, this, hdt::OBJECT);
    searchResultsModel = new SearchResultsModel(this, this);
    predicateStatus = new PredicateStatus(this, this);
}

HDTManager::~HDTManager() {
    closeHDT();
    delete subjectModel;
    delete predicateModel;
    delete objectModel;
    delete predicateStatus;
}

void HDTManager::updateOnHDTChanged()
{
    this->numResults = hdtCachedInfo->numResults;

    subjectModel->notifyLayoutChanged();
    predicateModel->notifyLayoutChanged();
    predicateStatus->refreshAll();
    objectModel->notifyLayoutChanged();
    searchResultsModel->updateResultListChanged();

    emit datasetChanged();
}

void HDTManager::importRDFFile(QString file, hdt::RDFNotation notation, hdt::HDTSpecification &spec)
{
    closeHDT();

    hdt::HDT *hdt = hdt::HDTFactory::createHDT(spec);
    HDTCachedInfo *hdtInfo = new HDTCachedInfo(hdt);
    HDTOperation *hdtop = new HDTOperation(hdt, hdtInfo);
    hdtop->loadFromRDF(file, notation);

    int result = hdtop->exec();

    delete hdtop;

    if(result==0) {
        this->hdt = hdt;
        this->hdtCachedInfo = hdtInfo;

        updateOnHDTChanged();
    } else {
        delete hdt;
        delete hdtInfo;
    }
}

void HDTManager::openHDTFile(QString file)
{
    closeHDT();

    hdt::HDT *hdt = hdt::HDTFactory::createDefaultHDT();
    HDTCachedInfo *hdtInfo = new HDTCachedInfo(hdt);
    HDTOperation *hdtop = new HDTOperation(hdt, hdtInfo);
    hdtop->loadFromHDT(file);

    int result = hdtop->exec();

    delete hdtop;

    if(result==0) {
        this->hdt = hdt;
        this->hdtCachedInfo = hdtInfo;

        updateOnHDTChanged();
    } else {
        delete hdt;
        delete hdtInfo;
    }
}

void HDTManager::saveHDTFile(QString file)
{
    if(hdt!=NULL) {
        HDTOperation *hdtop = new HDTOperation(hdt);
        hdtop->saveToHDT(file);
        hdtop->exec();

        delete hdtop;
    }
}

void HDTManager::exportRDFFile(QString file, hdt::RDFNotation notation)
{
    if(hdt!=NULL) { 
        HDTOperation *hdtop = new HDTOperation(hdt);
        hdtop->saveToRDF(file, notation);
        hdtop->exec();

        delete hdtop;
    }
}

void HDTManager::closeHDT()
{
    if(hdt!=NULL) {
        selectedTriple.clear();
        numResults = 0;
        delete hdt;
        hdt = NULL;

        subjectModel->notifyLayoutChanged();
        predicateModel->notifyLayoutChanged();
        predicateStatus->refreshAll();
        objectModel->notifyLayoutChanged();
        searchResultsModel->updateResultListChanged();

        emit datasetChanged();
    }
}


TripleComponentModel * HDTManager::getSubjectModel()
{
    return subjectModel;
}

TripleComponentModel * HDTManager::getPredicateModel()
{
    return predicateModel;
}
TripleComponentModel * HDTManager::getObjectModel()
{
    return objectModel;
}

SearchResultsModel * HDTManager::getSearchResultsModel()
{
    return searchResultsModel;
}

PredicateStatus *HDTManager::getPredicateStatus()
{
    return predicateStatus;
}

HDTCachedInfo *HDTManager::getHDTCachedInfo()
{
    return hdtCachedInfo;
}

hdt::HDT *HDTManager::getHDT()
{
    return hdt;
}

bool HDTManager::hasHDT()
{
    return hdt!=NULL;
}

void HDTManager::setSearchPattern(hdt::TripleString &pattern)
{
    if(hdt==NULL) {
        return;
    }

    if(searchPatternString != pattern) {
        try {
            this->searchPatternString = pattern;

            hdt->getDictionary().tripleStringtoTripleID(pattern, searchPatternID);

            if(!searchPatternID.isEmpty()) {
                if(iteratorResults!=NULL) {
                    delete iteratorResults;
                }
                iteratorResults = hdt->getTriples().search(searchPatternID);

                numResults=0;
                resultsTime.reset();
                updateNumResults();
            } else {
                numResults = hdt->getTriples().getNumberOfElements();
            }

            predicateStatus->selectPredicate(searchPatternID.getPredicate());
        } catch (char *exception){
            numResults = 0;
        }
        searchResultsModel->updateResultListChanged();

        emit searchPatternChanged();
    }
}

hdt::TripleID & HDTManager::getSearchPatternID()
{
    return searchPatternID;
}

hdt::TripleString & HDTManager::getSearchPatternString()
{
    return searchPatternString;
}

hdt::TripleID HDTManager::getSelectedTriple()
{
    return selectedTriple;
}

void HDTManager::setSelectedTriple(hdt::TripleID &selected)
{
    selectedTriple = selected;
}

void HDTManager::clearSelectedTriple()
{
    selectedTriple.clear();
}

quint64 inline DIST(quint64 x1, quint64 x2,
                quint64 y1, quint64 y2) {
        return (x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2);
}

void HDTManager::selectNearestTriple(int subject, int predicate, int object)
{
    vector<hdt::TripleID> &triples = getTriples();
    if(triples.size()==0) {
        this->clearSelectedTriple();
        return;
    }

    hdt::TripleID *best = &triples[0];
    unsigned int bestpos = 0;
    quint64 bestdist = DIST(triples[0].getSubject(), subject, triples[0].getObject(), object);

    for (unsigned int i = 0; i < triples.size(); i ++) {
        if (triples[i].match(searchPatternID) && predicateStatus->isPredicateActive(triples[i].getPredicate()-1)) {
            quint64 dist = DIST(triples[i].getSubject(), subject, triples[i].getObject(), object);

            if (dist < bestdist) {
                best = &triples[i];
                bestdist = dist;
                bestpos = i;
                //printf("1New %u, %u, %u, Dist: %u Pos: %u\n", best->getSubject(), best->getPredicate(), best->getObject(), bestdist, bestpos);
            }
        }
    }
    //printf("Found: %u, %u, %u, Dist: %llu\n", best->getSubject(), best->getPredicate(), best->getObject(), bestdist);

    this->setSelectedTriple( *best );
}

vector<hdt::TripleID> & HDTManager::getTriples()
{
    return hdtCachedInfo->triples;
}

unsigned int HDTManager::getNumResults()
{
    return numResults;
}

QString HDTManager::getTime()
{
    if(!hasHDT()) {
        return QString("0");
    }
    if(searchPatternID.isEmpty()) {
        return hdtCachedInfo->resultsTime.getRealStr().c_str();
    }
    return resultsTime.getRealStr().c_str();
}

void HDTManager::updateNumResults()
{
    if(iteratorResults==NULL) {
        return;
    }
    StopWatch cl;
    while(iteratorResults->hasNext()) {
        iteratorResults->next();
        numResults++;

#if 0
        cl.stop();
        if(cl.getReal()>100000) {
            break;
        }
#endif
    }

    resultsTime.stop();

    if(iteratorResults->hasNext()) {
        QTimer::singleShot(0, this, SLOT(updateNumResults()));
    } else {
        delete iteratorResults;
        iteratorResults = NULL;
    }

    searchResultsModel->updateNumResultsChanged();
    emit numResultsChanged(numResults);
}



