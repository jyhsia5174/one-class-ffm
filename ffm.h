#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstring>
#include <stdlib.h>
#include <unordered_set>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <climits>
#include <utility>
#include <numeric>
#include <cassert>


#include <immintrin.h>

#include<omp.h>
#include "mkl.h"



using namespace std;

typedef double ImpFloat;
typedef double ImpDouble;
typedef unsigned int ImpInt;
typedef unsigned long int ImpLong;
typedef vector<ImpDouble> Vec;

const int MIN_Z = -1000;

class Parameter {
public:
    ImpFloat omega, lambda, r;
    ImpInt nr_pass, k, nr_threads;
    string model_path, predict_path;
    bool self_side, freq = false;
    Parameter():omega(0.1), lambda(1e-5), r(-1), nr_pass(20), k(4), nr_threads(1), self_side(true) {};
};

class Node {
public:
    ImpInt fid;
    ImpLong idx;
    ImpDouble val;
    Node(): fid(0), idx(0), val(0) {};
};

class ImpData {
public:
    string file_name;
    ImpLong m, n, f, nnz_x, nnz_y;
    vector<ImpLong> nnx, nny;
    vector<Node> M, N;
    vector<Node*> X, Y;


    vector<vector<Node>> Ns;
    vector<vector<Node*>> Xs;
    vector<ImpLong> Ds;
    vector<vector<ImpLong>> freq;
    vector<ImpDouble> popular;

    ImpData(string file_name): file_name(file_name), m(0), n(0), f(0) {};
    void read(bool has_label, const ImpLong* ds=nullptr);
    void print_data_info();
    void split_fields();
    void transY(const vector<Node*> &YT);
};


class ImpProblem {
public:
    ImpProblem(shared_ptr<ImpData> &U, shared_ptr<ImpData> &Uva,
            shared_ptr<ImpData> &V, shared_ptr<Parameter> &param)
        :U(U), Uva(Uva), V(V), param(param) {};

    void init();
    void solve();
    ImpDouble func();

    void write_header(ofstream& o_f) const;
    void write_W_and_H(ofstream& o_f) const;
private:
    ImpDouble loss, lambda, w, r;

    shared_ptr<ImpData> U, Uva, V;
    shared_ptr<Parameter> param;

    ImpInt k, fu, fv, f;
    ImpLong m, n;
    ImpLong mt;

    vector<Vec> W, H, P, Q, Pva, Qva;
    Vec a, b, va_loss_prec, va_loss_ndcg, sa, sb;

    vector<ImpInt> top_k;

    void init_pair(const ImpInt &f12, const ImpInt &fi, const ImpInt &fj,
            const shared_ptr<ImpData> &d1, const shared_ptr<ImpData> &d2);

    void add_side(const Vec &p, const Vec &q, const ImpLong &m1, Vec &a1);
    void calc_side();
    void init_y_tilde();
    ImpDouble calc_cross(const ImpLong &i, const ImpLong &j);

    void update_side(const bool &sub_type, const Vec &S, const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1);
    void update_cross(const bool &sub_type, const Vec &S, const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1);

    void UTx(const Node *x0, const Node* x1, const Vec &A, ImpDouble *c);
    void UTX(const vector<Node*> &X, ImpLong m1, const Vec &A, Vec &C);
    void QTQ(const Vec &C, const ImpLong &l);
    ImpDouble pq(const ImpInt &i, const ImpInt &j,const ImpInt &f1, const ImpInt &f2);
    ImpDouble norm_block(const ImpInt &f1,const ImpInt &f2);

    void solve_side(const ImpInt &f1, const ImpInt &f2);
    void gd_side(const ImpInt &f1, const Vec &W1, const Vec &Q1, Vec &G);
    void hs_side(const ImpLong &m1, const ImpLong &n1, const Vec &S, Vec &HS, const Vec &Q1, const vector<Node*> &UX, const vector<Node*> &Y, Vec &Hv_);

    void solve_cross(const ImpInt &f1, const ImpInt &f2);
    void gd_cross(const ImpInt &f1, const ImpInt &f12, const Vec &Q1, const Vec &W1, Vec &G);
    void hs_cross(const ImpLong &m1, const ImpLong &n1, const Vec &V, const Vec &VQTQ, Vec &Hv, const Vec &Q1, const vector<Node*> &UX, const vector<Node*> &Y, Vec &Hv_);

    void cg(const ImpInt &f1, const ImpInt &f2, Vec &W1, const Vec &Q1, const Vec &G, Vec &P1);
    void cache_sasb();


    void one_epoch();
    void init_va(ImpInt size);

    void pred_z(const ImpLong i, ImpDouble *z);
    void pred_items();
    void prec_k(ImpDouble *z, ImpLong i, vector<ImpLong> &hit_counts);
    void ndcg(ImpDouble *z, ImpLong i, vector<ImpDouble> &hit_counts);
    void validate();
    void print_epoch_info(ImpInt t);

};


void save_model(const ImpProblem & prob, string & model_path );
