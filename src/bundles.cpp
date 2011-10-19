//
//  bundles.cpp
//  express
//
//  Created by Adam Roberts on 9/6/11.
//  Copyright 2011 Adam Roberts. All rights reserved.
//

#include "transcripts.h"
#include "bundles.h"
#include "main.h"

using namespace std;

void CovarTable::increment(TransID trans1, TransID trans2, double incr_amt)
{
    size_t pair_id = size()*min(trans1, trans2)+max(trans1, trans2);
    if (_covar_map.count(pair_id))
    {
            _covar_map[pair_id] = log_sum(_covar_map[pair_id], incr_amt);
    }
    else
    {
        _covar_map[pair_id] = incr_amt;
    }
}


double CovarTable::get(TransID trans1, TransID trans2)
{
    size_t pair_id = size()*min(trans1, trans2)+max(trans1, trans2);
    if (_covar_map.count(pair_id))
    {
        return _covar_map[pair_id];
    }
    else
    {
        return HUGE_VAL;
    }
}

Bundle::Bundle(Transcript* trans)
: _counts(trans->tot_counts())
{ _transcripts.push_back(trans); }

void Bundle::incr_counts(size_t incr_amt)
{
    _counts += incr_amt;
}

BundleTable::~BundleTable()
{
    foreach(Bundle* bundle, _bundles)
    {
        delete bundle;
    }
}

Bundle* BundleTable::create_bundle(Transcript* trans)
{
    Bundle* b = new Bundle(trans);
    _bundles.insert(b);
    return b;
}

Bundle* BundleTable::merge(Bundle* b1, Bundle* b2)
{
    if (b1==b2)
        return b1;
    
    if (b1->size() < b2->size())
        swap(b1, b2);

  
    foreach(Transcript* trans, b2->transcripts())
    {
        trans->bundle(b1);
        b1->transcripts().push_back(trans);
    }
    
    b1->incr_counts(b2->counts());
    _bundles.erase(b2);
    delete b2;  
    
    return b1;
}