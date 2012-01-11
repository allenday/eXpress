//
//  transcripts.cpp
//  express
//
//  Created by Adam Roberts on 3/20/11.
//  Copyright 2011 Adam Roberts. All rights reserved.
//

#include "main.h"
#include "transcripts.h"
#include "fld.h"
#include "fragments.h"
#include "biascorrection.h"
#include "mismatchmodel.h"
#include <iostream>
#include <fstream>
#include <math.h>
#include <assert.h>
#include <stdio.h>

using namespace std;


Transcript::Transcript(const TransID id, const std::string& name, const std::string& seq, double alpha, const Globals* globs)
:   _globs(globs),
    _id(id),
    _name(name),
    _seq(seq),
    _alpha(log(alpha)),
    _mass(HUGE_VAL),    
    _ambig_mass(HUGE_VAL),
    _binom_var(HUGE_VAL),
    _samp_var(HUGE_VAL),
    _tot_mass(HUGE_VAL),
    _tot_ambig_mass(HUGE_VAL),
    _tot_unc(HUGE_VAL),
    _est_counts(HUGE_VAL),
    _est_counts_var(HUGE_VAL),
    _uniq_counts(0),
    _tot_counts(0),
    _avg_bias(0)
{ 
    if (globs->bias_table)
    {
        _start_bias = new std::vector<float>(seq.length(),0);
        _end_bias = new std::vector<float>(seq.length(),0);
    }
    else
    {
        _start_bias = NULL;
        _end_bias = NULL;
    }
    _cached_eff_len = est_effective_length();
}

void Transcript::add_mass(double p, double v, double mass) 
{ 
    _mass = log_sum(_mass, p+mass);
    _tot_mass = log_sum(_mass, mass);
    _samp_var = log_sum(_samp_var, 2*mass+p);
    if (p != 0.0)
    {
        _binom_var = log_sum(_binom_var, 2*mass + p + log(1-sexp(p)));
        _tot_unc = log_sum(_tot_unc, v+mass);
        _ambig_mass = log_sum(_ambig_mass, mass+p);
        if (p!=HUGE_VAL)
            _tot_ambig_mass = log_sum(_tot_ambig_mass, mass);
    }
}  

//FIX
void Transcript::add_prob_count(double p)
{
    _est_counts = log_sum(_est_counts, p);
    if (p != 0.0)
        _est_counts_var = log_sum(_est_counts_var, p + log(1-sexp(p)));
}

void Transcript::round_reset()
{
    _mass = _est_counts;
    _binom_var = _est_counts_var;
    _est_counts = HUGE_VAL;
    _est_counts_var = HUGE_VAL;
}

double Transcript::mass(bool with_pseudo) const
{
    if (!with_pseudo)
        return _mass;
    boost::mutex::scoped_lock lock(_bias_lock);
    return log_sum(_mass, _alpha+_cached_eff_len);
}

double Transcript::log_likelihood(const FragHit& frag, bool with_pseudo) const
{

    double ll = mass();
    
    if (_globs->mismatch_table)
        ll += (_globs->mismatch_table)->log_likelihood(frag);
    
    const PairStatus ps = frag.pair_status();
    {
        boost::mutex::scoped_lock lock(_bias_lock);
        
        if (with_pseudo)
            ll = log_sum(ll, _alpha+_cached_eff_len);
        if (_globs->bias_table)
        {
            if (ps != RIGHT_ONLY)
                ll += _start_bias->at(frag.left);
            if (ps != LEFT_ONLY)
                ll += _end_bias->at(frag.right-1);  
        }
        ll -= _cached_eff_len;
    }
    
    if (ps == PAIRED)
        ll += (_globs->fld)->pdf(frag.length());
    
    assert(!(isnan(ll)||isinf(ll)));
    return ll;
}

double Transcript::est_effective_length() const
{
    double eff_len = HUGE_VAL;
    
    for(size_t l = 1; l <= min(length(), (_globs->fld)->max_val()); l++)
    {
        eff_len = log_sum(eff_len, (_globs->fld)->pdf(l)+log((double)length()-l+1));
    }
    
    boost::mutex::scoped_lock lock(_bias_lock);
    eff_len += _avg_bias;
    return eff_len;
}

double Transcript::cached_effective_length() const
{
    return _cached_eff_len;
}

//double Transcript::effective_length() const
//{
//    double eff_len = 0.0;
//    boost::mutex::scoped_lock lock(_bias_lock);
//    
//    for(size_t l = 1; l <= min(length(), (_globs->fld)->max_val()); l++)
//    {
//        double len_bias = 0;
//        for (size_t i = 0; i < length()-l+1; i++)
//        {
//            len_bias = log_sum(len_bias, _start_bias[i] + _end_bias[i+l]);
//        }
//        eff_len += log_sum(eff_len, (_globs->fld)->pdf(l) + len_bias);
//    }
//    
//    return eff_len;
//}


void Transcript::update_transcript_bias()
{
    if (_globs->bias_table)
    {
        boost::mutex::scoped_lock lock(_bias_lock);
        _avg_bias = (_globs->bias_table)->get_transcript_bias(*_start_bias, *_end_bias, *this);
    }
    _cached_eff_len = est_effective_length();
}

TranscriptTable::TranscriptTable(const string& trans_fasta_file, const TransIndex& trans_index, const TransIndex& trans_lengths, double alpha, const Globals* globs)
: _globs(globs),
  _trans_map(trans_index.size(), NULL),
  _alpha(alpha)
{
    cout << "Loading target sequences";
    if (globs->bias_table)
        cout << " and measuring bias background";
    cout << "...\n\n";
    
    boost::unordered_set<string> target_names;
    
    ifstream infile (trans_fasta_file.c_str());
    string line;
    string seq = "";
    string name = "";
    if (infile.is_open())
    {
        while ( infile.good() )
        {
            getline (infile, line, '\n');
            if (line[0] == '>')
            {
                if (!name.empty())
                {
                    add_trans(name, seq, trans_index, trans_lengths);
                }
                name = line.substr(1,line.find(' ')-1);
                if (target_names.count(name))
                {
                    cerr << "ERROR: Target '" << name << "' is duplicated in the input FASTA.  Ensure all target names are unique and re-map before re-running eXpress\n";
                    exit(1);
                }
                target_names.insert(name);
                seq = "";
            }
            else
            {
                seq += line;
            }
            
        }
        if (!name.empty())
        {
            add_trans(name, seq, trans_index, trans_lengths);
        }
        infile.close();
        if (globs->bias_table)
        {
            globs->bias_table->normalize_expectations();
        }
    }
    else 
    {
        cerr << "ERROR: Unable to open MultiFASTA file '" << trans_fasta_file << "'.\n" ; 
        exit(1);
    }
    
    if (size() == 0)
    {
        cerr << "ERROR: No targets found in MultiFASTA file '" << trans_fasta_file << "'.\n" ; 
        exit(1);        
    }
    
    for(TransIndex::const_iterator it = trans_index.begin(); it != trans_index.end(); ++it)
    {
        if (!_trans_map[it->second])
        {
            cerr << "ERROR: Sequence for target '" << it->first << "' not found in MultiFasta file '" << trans_fasta_file << "'.\n";
            exit(1);
        }
    }
}

TranscriptTable::~TranscriptTable()
{
    foreach( Transcript* trans, _trans_map)
    {
        delete trans;
    }
}

void TranscriptTable::add_trans(const string& name, const string& seq, const TransIndex& trans_index, const TransIndex& trans_lengths)
{
    TransIndex::const_iterator it = trans_index.find(name);
    if(it == trans_index.end())
    {
        cerr << "Warning: Target '" << name << "' exists in MultiFASTA but not alignment (SAM/BAM) file.\n";
        return;
    }
    
    if (trans_lengths.find(name)->second != seq.length())
    {
        cerr << "ERROR: Target '" << name << "' differs in length between MultiFASTA and alignment (SAM/BAM) files ("<< seq.length() << " vs. " << trans_lengths.find(name)->second << ").\n";
        exit(1);
    }

    Transcript* trans = new Transcript(it->second, name, seq, _alpha, _globs);
    if (_globs->bias_table)
        (_globs->bias_table)->update_expectations(*trans);
    _trans_map[trans->id()] = trans;
    trans->bundle(_bundle_table.create_bundle(trans));
}

Transcript* TranscriptTable::get_trans(TransID id)
{
    return _trans_map[id];
}

Bundle* TranscriptTable::merge_bundles(Bundle* b1, Bundle* b2)
{
    if (b1 != b2)
    {
        return _bundle_table.merge(b1, b2);
    }
    return b1;
}

size_t TranscriptTable::num_bundles()
{
    return _bundle_table.size();
}

void TranscriptTable::round_reset()
{
    foreach(Transcript* trans, _trans_map)
    {
        trans->round_reset();
    }
}

void TranscriptTable::threaded_bias_update()
{
    while(running)
    {
        foreach(Transcript* trans, _trans_map)
        {  
            trans->update_transcript_bias();
            if (!running)
                break;
        }
    }
}

void project_to_polytope(vector<Transcript*> bundle_trans, vector<double>& trans_counts, double bundle_counts)
{
    vector<bool> polytope_bound(bundle_trans.size(), false);
    
    while (true)
    {
        double unbound_counts = 0;
        double bound_counts = 0;
        for (size_t i = 0; i < bundle_trans.size(); ++i)
        {
            Transcript& trans = *bundle_trans[i];
            
            if (trans_counts[i] > trans.tot_counts())
            {
                trans_counts[i] = trans.tot_counts();
                polytope_bound[i] = true;
            }
            else if (trans_counts[i] < trans.uniq_counts())
            {
                trans_counts[i] = trans.uniq_counts();
                polytope_bound[i] = true;
            }
            
            if (polytope_bound[i])
            {
                bound_counts += trans_counts[i];
            }
            else
            {
                unbound_counts += trans_counts[i];
            }
        }
        
        if (unbound_counts + bound_counts == bundle_counts)
            return;
        
        double normalizer = (bundle_counts - bound_counts)/unbound_counts;
        bool unbound_exist = false;
        unbound_counts = 0;
        for (size_t i = 0; i < bundle_trans.size(); ++i)
        {    
            if (!polytope_bound[i])
            {
                trans_counts[i] *= normalizer;
                unbound_counts += trans_counts[i];
                unbound_exist = true;
            }
        }
        
        if (unbound_counts + bound_counts - bundle_counts < EPSILON)
            return;
        
        if (!unbound_exist)
            polytope_bound = vector<bool>(bundle_trans.size(), false);
    }
}

void TranscriptTable::output_results(string output_dir, size_t tot_counts, bool output_varcov)
{ 
    FILE * expr_file = fopen((output_dir + "/results.xprs").c_str(), "w");
    ofstream varcov_file;
    if (output_varcov)
        varcov_file.open((output_dir + "/varcov.xprs").c_str());    
    
    fprintf(expr_file, "bundle_id\ttarget_id\tlength\teff_length\ttot_counts\tuniq_counts\tpost_count_mean\tpost_count_var\tfpkm\tfpkm_conf_low\tfpkm_conf_high\n");

    double l_bil = log(1000000000.);
    double l_tot_counts = log((double)tot_counts);
    
    size_t bundle_id = 0;
    foreach (Bundle* bundle, _bundle_table.bundles())
    {
        ++bundle_id;
        
        const vector<Transcript*>& bundle_trans = bundle->transcripts();
        
        if (output_varcov)
        {
            varcov_file << ">" << bundle_id << ": ";
            for (size_t i = 0; i < bundle_trans.size(); ++i)
            {
                if (i)
                    varcov_file << ", ";
                varcov_file << bundle_trans[i]->name();
            }
            varcov_file << endl;
        }        
        
        // Calculate total counts for bundle and bundle-level rho
        double l_bundle_mass = HUGE_VAL;
        for (size_t i = 0; i < bundle_trans.size(); ++i)
        {
            l_bundle_mass = log_sum(l_bundle_mass, bundle_trans[i]->mass()); 
        }
        
        if (bundle->counts())
        {
            double l_bundle_counts = log((double)bundle->counts());
            double l_var_renorm = 2*(l_bundle_counts - l_bundle_mass);
            
            vector<double> trans_counts(bundle_trans.size(),0);
            bool requires_projection = false;

            for (size_t i = 0; i < bundle_trans.size(); ++i)
            {
                Transcript& trans = *bundle_trans[i];
                double l_trans_frac = trans.mass() - l_bundle_mass;
                trans_counts[i] = sexp(l_trans_frac + l_bundle_counts);
                if (trans_counts[i] - (double)trans.tot_counts() > EPSILON ||  (double)trans.uniq_counts() - trans_counts[i] > EPSILON)
                    requires_projection = true;
            }
            
            
            if (bundle_trans.size() > 1 && requires_projection)
            {
                project_to_polytope(bundle_trans, trans_counts, bundle->counts());
            }
            
            
            // Calculate individual counts and rhos
            for (size_t i = 0; i < bundle_trans.size(); ++i)
            {
                Transcript& trans = *bundle_trans[i];
                double l_eff_len = trans.est_effective_length();

                // Calculate count variance
                double count_var = 0;
                
                double p=0;
                double v=0;
                double n=0;
                double a=0;
                double b=0;
                double binom_var=0;
                
                if (trans.tot_counts() != trans.uniq_counts())
                {
                    binom_var = min(sexp(trans.binom_var() + l_var_renorm), 0.25*trans.tot_counts());
                    p = sexp(trans.ambig_mass() - trans.tot_ambig_mass());
                    v = sexp(trans.tot_uncertainty() - trans.tot_ambig_mass());
                    assert (p >=0 && p <= 1);
                    n = trans.tot_counts()-trans.uniq_counts();
                    a = p*(p*(1-p)/v - 1);
                    b = (1-p)*(p*(1-p)/v -1);
                    if (v == 0 || a < 0 || b < 0)
                        count_var = binom_var;
                    else
                        count_var = n*a*b*(a+b+n)/((a+b)*(a+b)*(a+b+1));
                    assert(!isnan(count_var) && !isinf(count_var));
                }
                
                double fpkm_std_dev = sqrt(trans_counts[i] + count_var);
                double fpkm_constant = sexp(l_bil - l_eff_len - l_tot_counts);
                double trans_fpkm = trans_counts[i] * fpkm_constant;
                double fpkm_lo = max(0.0, (trans_counts[i] - 2*fpkm_std_dev) * fpkm_constant);
                double fpkm_hi = (trans_counts[i] + 2*fpkm_std_dev) * fpkm_constant;
                
                fprintf(expr_file, "" SIZE_T_FMT "\t%s\t" SIZE_T_FMT "\t%f\t" SIZE_T_FMT "\t" SIZE_T_FMT "\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\t%f\n", bundle_id, trans.name().c_str(), trans.length(), sexp(l_eff_len), trans.tot_counts(), trans.uniq_counts(), trans_counts[i], count_var,trans_fpkm, fpkm_lo, fpkm_hi, p,v,n,a,b,binom_var);
            
                if (output_varcov)
                {
                    for (size_t j = 0; j < bundle_trans.size(); ++j)
                    {
                        if (j)
                            varcov_file << "\t";
                        
                        if (i==j)
                            varcov_file << scientific << count_var;
                        else
                            varcov_file << scientific << -sexp(get_covar(trans.id(), bundle_trans[j]->id()) + l_var_renorm);
                           
                    }
                    varcov_file << endl;
                }
            }
        }
        else
        {
            for (size_t i = 0; i < bundle_trans.size(); ++i)
            {
                Transcript& trans = *bundle_trans[i];
                fprintf(expr_file, "" SIZE_T_FMT "\t%s\t" SIZE_T_FMT "\t%f\t%d\t%d\t%f\t%f\t%f\t%f\t%f\n", bundle_id, trans.name().c_str(), trans.length(), sexp(trans.est_effective_length()), 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0);
                
                if (output_varcov)
                {
                    for (size_t j = 0; j < bundle_trans.size(); ++j)
                    {
                        if (j)
                            varcov_file << "\t";
                        varcov_file << scientific << 0.0;
                    }
                    varcov_file << endl;
                }
            }   
        }

    }
    fclose(expr_file);
    if (output_varcov)
        varcov_file.close();
}
