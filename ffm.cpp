#include "ffm.h"

ImpDouble qrsqrt(ImpDouble x)
{
    ImpDouble xhalf = 0.5*x;
    ImpLong i;
    memcpy(&i, &x, sizeof(i));
    i = 0x5fe6eb50c7b537a9 - (i>>1);
    memcpy(&x, &i, sizeof(i));
    x = x*(1.5 - xhalf*x*x);
    return x;
}

ImpDouble sum(const Vec &v) {
    ImpDouble sum = 0;
    for (ImpDouble val: v)
        sum += val;
    return sum;
}

void axpy(const ImpDouble *x, ImpDouble *y, const ImpLong &l, const ImpDouble &lambda) {
    cblas_daxpy(l, lambda, x, 1, y, 1);
}

void scal(ImpDouble *x, const ImpLong &l, const ImpDouble &lambda) {
    cblas_dscal(l, lambda, x, 1);
}

void mm(const ImpDouble *a, const ImpDouble *b, ImpDouble *c,
        const ImpLong l, const ImpLong n, const ImpInt k) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
            l, n, k, 1, a, k, b, n, 0, c, n);
}

void mm(const ImpDouble *a, const ImpDouble *b, ImpDouble *c,
        const ImpLong l, const ImpLong n, const ImpInt k, const ImpDouble &beta) {
    cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
            l, n, k, 1, a, k, b, n, beta, c, n);
}

void mm(const ImpDouble *a, const ImpDouble *b, ImpDouble *c,
        const ImpLong k, const ImpInt l) {
    cblas_dgemm(CblasRowMajor, CblasTrans, CblasNoTrans,
            k, k, l, 1, a, k, b, k, 0, c, k);
}

void mv(const ImpDouble *a, const ImpDouble *b, ImpDouble *c,
        const ImpLong l, const ImpInt k, const ImpDouble &beta, bool trans) {
    const CBLAS_TRANSPOSE CBTr= (trans)? CblasTrans: CblasNoTrans;
    cblas_dgemv(CblasRowMajor, CBTr, l, k, 1, a, k, b, 1, beta, c, 1);
}

const ImpInt index_vec(const ImpInt f1, const ImpInt f2, const ImpInt f) {
    return f2 + (f-1)*f1 - f1*(f1-1)/2;
}

ImpDouble inner(const ImpDouble *p, const ImpDouble *q, const ImpInt k)
{
    __m128d XMM = _mm_setzero_pd();
    for(ImpInt d = 0; d < k; d += 2)
        XMM = _mm_add_pd(XMM, _mm_mul_pd(
                  _mm_load_pd(p+d), _mm_load_pd(q+d)));
    XMM = _mm_hadd_pd(XMM, XMM);
    ImpDouble product;
    _mm_store_sd(&product, XMM);
    return product;
}

void hadmard_product(const Vec &V1, const Vec &V2, const ImpInt &row,
        const ImpInt &col,const ImpDouble &alpha, Vec &vv){
    const ImpDouble *v1p = V1.data(), *v2p = V2.data();

    #pragma omp parallel for schedule(guided)
    for(ImpInt i = 0; i < row; i++)
        vv[i] += alpha*inner(v1p+i*col, v2p+i*col, col);
}

void init_mat(Vec &vec, const ImpLong nr_rows, const ImpLong nr_cols) {
    default_random_engine ENGINE(rand());
    vec.resize(nr_rows*nr_cols, 0.1);
    uniform_real_distribution<ImpDouble> dist(0, 0.1*qrsqrt(nr_cols));

    auto gen = std::bind(dist, ENGINE);
    generate(vec.begin(), vec.end(), gen);
}

void ImpData::read(bool has_label, const ImpLong *ds) {
    ifstream fs(file_name);
    string line, label_block, label_str;
    char dummy;

    ImpLong fid, idx, y_nnz=0, x_nnz=0;
    ImpDouble val;

    while (getline(fs, line)) {
        m++;
        istringstream iss(line);

        if (has_label) {
            iss >> label_block;
            istringstream labelst(label_block);
            while (getline(labelst, label_str, ',')) {
                idx = stoi(label_str);
                n = max(n, idx+1);
                y_nnz++;
            }
        }

        while (iss >> fid >> dummy >> idx >> dummy >> val) {
            f = max(f, fid+1);
            if (ds!= nullptr && ds[fid] <= idx)
                continue;
            x_nnz++;
        }
    }

    fs.clear();
    fs.seekg(0);

    nnz_x = x_nnz;
    N.resize(x_nnz);

    X.resize(m+1);
    Y.resize(m+1);

    if (has_label) {
        nnz_y = y_nnz;
        M.resize(y_nnz);
    }

    vector<ImpInt> nnx(m, 0), nny(m, 0);

    ImpLong nnz_i=0, nnz_j=0, i=0;

    while (getline(fs, line)) {
        istringstream iss(line);

        if (has_label) {
            iss >> label_block;
            istringstream labelst(label_block);
            while (getline(labelst, label_str, ',')) {
                nnz_j++;
                ImpLong idx = stoi(label_str);
                M[nnz_j-1].idx = idx;
            }
            nny[i] = nnz_j;
        }

        while (iss >> fid >> dummy >> idx >> dummy >> val) {
            if (ds!= nullptr && ds[fid] <= idx)
                continue;
            nnz_i++;
            N[nnz_i-1].fid = fid;
            N[nnz_i-1].idx = idx;
            N[nnz_i-1].val = val;
        }
        nnx[i] = nnz_i;
        i++;
    }

    X[0] = N.data();
    for (ImpLong i = 0; i < m; i++) {
        X[i+1] = N.data() + nnx[i];
    }

    if (has_label) {
        Y[0] = M.data();
        for (ImpLong i = 0; i < m; i++) {
            Y[i+1] = M.data() + nny[i];
        }
    }
    fs.close();
}

void ImpData::split_fields() {
    Ns.resize(f);
    Xs.resize(f);
    Ds.resize(f);
    freq.resize(f);


    vector<ImpLong> f_sum_nnz(f, 0);
    vector<vector<ImpLong>> f_nnz(f);

    for (ImpInt fi = 0; fi < f; fi++) {
        Ds[fi] = 0;
        f_nnz[fi].resize(m, 0);
        Xs[fi].resize(m+1);
    }

    for (ImpLong i = 0; i < m; i++) {
        for (Node* x = X[i]; x < X[i+1]; x++) {
            ImpInt fid = x->fid;
            f_sum_nnz[fid]++;
            f_nnz[fid][i]++;
        }
    }

    for (ImpInt fi = 0; fi < f; fi++) {
        Ns[fi].resize(f_sum_nnz[fi]);
        f_sum_nnz[fi] = 0;
    }

    for (ImpLong i = 0; i < m; i++) {
        for (Node* x = X[i]; x < X[i+1]; x++) {
            ImpInt fid = x->fid;
            ImpLong idx = x->idx;
            ImpDouble val = x->val;

            f_sum_nnz[fid]++;
            Ds[fid] = max(idx+1, Ds[fid]);
            ImpLong nnz_i = f_sum_nnz[fid]-1;

            Ns[fid][nnz_i].fid = fid;
            Ns[fid][nnz_i].idx = idx;
            Ns[fid][nnz_i].val = val;
        }
    }

    for(ImpInt fi = 0; fi < f; fi++){
        freq[fi].resize(Ds[fi]);
        fill(freq[fi].begin(), freq[fi].end(), 0);
    }

    for( ImpLong i = 0; i < m; i++){
        for(Node* x = X[i]; x < X[i+1]; x++){
            ImpInt fid = x->fid;
            ImpLong idx = x->idx;
            freq[fid][idx]++;
        }
    }
    for (ImpInt fi = 0; fi < f; fi++) {
        Node* fM = Ns[fi].data();
        Xs[fi][0] = fM;
        ImpLong start = 0;
        for (ImpLong i = 0; i < m; i++) {
            Xs[fi][i+1] = fM + start + f_nnz[fi][i];
            start += f_nnz[fi][i];
        }
    }

    X.clear();
    X.shrink_to_fit();

    N.clear();
    N.shrink_to_fit();
}

void ImpData::transY(const vector<Node*> &YT) {
    n = YT.size() - 1;
    vector<pair<ImpLong, Node*>> perm;
    ImpLong nnz = 0;
    vector<ImpLong> nnzs(m, 0);

    for (ImpLong i = 0; i < n; i++)
        for (Node* y = YT[i]; y < YT[i+1]; y++) {
            if (y->idx >= m )
              continue;
            nnzs[y->idx]++;
            perm.emplace_back(i, y);
            nnz++;
        }

    auto sort_by_column = [&] (const pair<ImpLong, Node*> &lhs,
            const pair<ImpLong, Node*> &rhs) {
        return tie(lhs.second->idx, lhs.first) < tie(rhs.second->idx, rhs.first);
    };

    sort(perm.begin(), perm.end(), sort_by_column);

    M.resize(nnz);
    nnz_y = nnz;
    for (ImpLong nnz_i = 0; nnz_i < nnz; nnz_i++) {
        M[nnz_i].idx = perm[nnz_i].first;
        M[nnz_i].val = perm[nnz_i].second->val;
    }

    Y[0] = M.data();
    ImpLong start_idx = 0;
    for (ImpLong i = 0; i < m; i++) {
        start_idx += nnzs[i];
        Y[i+1] = M.data()+start_idx;
    }
}

void ImpData::print_data_info() {
    cout << "File:";
    cout << file_name;
    cout.width(12);
    cout << "m:";
    cout << m;
    cout.width(12);
    cout << "n:";
    cout << n;
    cout.width(12);
    cout << "f:";
    cout << f;
    cout.width(12);
    cout << "d:";
    cout << Ds[0];
    cout << endl;
}

void ImpProblem::UTx(const Node* x0, const Node* x1, const Vec &A, ImpDouble *c) {
    for (const Node* x = x0; x < x1; x++) {
        const ImpLong idx = x->idx;
        const ImpDouble val = x->val;
        for (ImpInt d = 0; d < k; d++) {
            ImpLong jd = idx*k+d;
            c[d] += val*A[jd];
        }
    }
}

void ImpProblem::UTX(const vector<Node*> &X, const ImpLong m1, const Vec &A, Vec &C) {
    fill(C.begin(), C.end(), 0);
    ImpDouble* c = C.data();
#pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < m1; i++)
        UTx(X[i], X[i+1], A, c+i*k);
}


void ImpProblem::init_pair(const ImpInt &f12,
        const ImpInt &fi, const ImpInt &fj,
        const shared_ptr<ImpData> &d1,
        const shared_ptr<ImpData> &d2) {
    const ImpLong Df1 = d1->Ds[fi];
    const ImpLong Df2 = d2->Ds[fj];

    const vector<Node*> &X1 = d1->Xs[fi];
    const vector<Node*> &X2 = d2->Xs[fj];

    init_mat(W[f12], Df1, k);
    init_mat(H[f12], Df2, k);
    P[f12].resize(d1->m*k, 0);
    Q[f12].resize(d2->m*k, 0);
    UTX(X1, d1->m, W[f12], P[f12]);
    UTX(X2, d2->m, H[f12], Q[f12]);
}

void ImpProblem::add_side(const Vec &p, const Vec &q, const ImpLong &m1, Vec &a1) {
    const ImpDouble *pp = p.data(), *qp = q.data();
    for (ImpLong i = 0; i < m1; i++) {
        const ImpDouble *pi = pp+i*k, *qi = qp+i*k;
        a1[i] += inner(pi, qi, k);
    }
}

void ImpProblem::calc_side() {
    for (ImpInt f1 = 0; f1 < fu; f1++) {
        for (ImpInt f2 = f1; f2 < fu; f2++) {
            const ImpInt f12 = index_vec(f1, f2, f);
            add_side(P[f12], Q[f12], m, a);
        }
    }
    for (ImpInt f1 = fu; f1 < f; f1++) {
        for (ImpInt f2 = f1; f2 < f; f2++) {
            const ImpInt f12 = index_vec(f1, f2, f);
            add_side(P[f12], Q[f12], n, b);
        }
    }
}

ImpDouble ImpProblem::calc_cross(const ImpLong &i, const ImpLong &j) {
    ImpDouble cross_value = 0.0;
    for (ImpInt f1 = 0; f1 < fu; f1++) {
        for (ImpInt f2 = fu; f2 < f; f2++) {
            const ImpInt f12 = index_vec(f1, f2, f);
            const ImpDouble *pp = P[f12].data();
            const ImpDouble *qp = Q[f12].data();
            cross_value += inner(pp+i*k, qp+j*k, k);
        }
    }
    return cross_value;
}

void ImpProblem::init_y_tilde() {
    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < m; i++) {
        for (Node* y = U->Y[i]; y < U->Y[i+1]; y++) {
            ImpLong j = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j) - 1;
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < n; j++) {
        for (Node* y = V->Y[j]; y < V->Y[j+1]; y++) {
            ImpLong i = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j) - 1;
        }
    }
}

void ImpProblem::update_side(const bool &sub_type, const Vec &S
        , const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1) {
    // Update W1 and P1
    axpy( S.data(), W1.data(), S.size(), 1);
    const ImpLong m1 = (sub_type)? m : n;
    UTX(X12, m1, W1, P1);

    // Update y_tilde and pq
    Vec &a1 = (sub_type)? a:b;
    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    shared_ptr<ImpData> V1 = (sub_type)? V:U;

    Vec gaps(m1, 0);
    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);
    hadmard_product(XS, Q1, m1, k, 1, gaps);

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < U1->m; i++) {
        a1[i] += gaps[i];
        for (Node* y = U1->Y[i]; y < U1->Y[i+1]; y++) {
            y->val += gaps[i];
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < V1->m; j++) {
        for (Node* y = V1->Y[j]; y < V1->Y[j+1]; y++) {
            const ImpLong i = y->idx;
            y->val += gaps[i];
        }
    }
}

void ImpProblem::update_cross(const bool &sub_type, const Vec &S,
        const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1) {
    axpy( S.data(), W1.data(), S.size(), 1);
    const ImpLong m1 = (sub_type)? m : n;
    UTX(X12, m1, W1, P1);

    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    shared_ptr<ImpData> V1 = (sub_type)? V:U;

    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < U1->m; i++) {
        for (Node* y = U1->Y[i]; y < U1->Y[i+1]; y++) {
            const ImpLong j = y->idx;
            y->val += inner( XS.data()+i*k, Q1.data()+j*k, k);
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < V1->m; j++) {
        for (Node* y = V1->Y[j]; y < V1->Y[j+1]; y++) {
            const ImpLong i = y->idx;
            y->val += inner( XS.data()+i*k, Q1.data()+j*k, k);
        }
    }
}

void ImpProblem::init() {
    lambda = param->lambda;
    w = param->omega;
    r = param->r;

    m = U->m;
    n = V->m;

    fu = U->f;
    fv = V->f;
    f = fu+fv;

    k = param->k;

    a.resize(m, 0);
    b.resize(n, 0);

    sa.resize(m, 0);
    sb.resize(n, 0);

    const ImpInt nr_blocks = f*(f+1)/2;

    W.resize(nr_blocks);
    H.resize(nr_blocks);

    P.resize(nr_blocks);
    Q.resize(nr_blocks);

    for (ImpInt f1 = 0; f1 < f; f1++) {
        const shared_ptr<ImpData> d1 = ((f1<fu)? U: V);
        const ImpInt fi = ((f1>=fu)? f1-fu: f1);
        for (ImpInt f2 = f1; f2 < f; f2++) {
            const shared_ptr<ImpData> d2 = ((f2<fu)? U: V);
            const ImpInt fj = ((f2>=fu)? f2-fu: f2);
            const ImpInt f12 = index_vec(f1, f2, f);
            init_pair(f12, fi, fj, d1, d2);
        }
    }

    cache_sasb();
    if (param->self_side)
        calc_side();
    init_y_tilde();
}

void ImpProblem::cache_sasb() {
    fill(sa.begin(), sa.end(), 0);
    fill(sb.begin(), sb.end(), 0);

    const Vec o1(m, 1), o2(n, 1);
    Vec tk(k);

    for (ImpInt f1 = 0; f1 < fu; f1++) {
        for (ImpInt f2 = fu; f2 < f; f2++) {
            const ImpInt f12 = index_vec(f1, f2, f);
            const Vec &P1 = P[f12], &Q1 = Q[f12];

            fill(tk.begin(), tk.end(), 0);
            mv(Q1.data(), o2.data(), tk.data(), n, k, 0, true);
            mv(P1.data(), tk.data(), sa.data(), m, k, 1, false);

            fill(tk.begin(), tk.end(), 0);
            mv(P1.data(), o1.data(), tk.data(), m, k, 0, true);
            mv(Q1.data(), tk.data(), sb.data(), n, k, 1, false);
        }
    }
}

void ImpProblem::gd_side(const ImpInt &f1, const Vec &W1, const Vec &Q1, Vec &G) {

    const shared_ptr<ImpData> U1 = (f1 < fu)? U:V;
    const vector<Node*> &Y = U1->Y;

    const ImpInt base = (f1 < fu)? 0: fu;
    const ImpInt fi = f1-base;
    const vector<Node*> &X = U1->Xs[fi];

    const ImpLong m1 = (f1 < fu)? m:n;
    const ImpLong n1 = (f1 < fu)? n:m;

    const Vec &a1 = (f1 < fu)? a:b;
    const Vec &b1 = (f1 < fu)? b:a;
    const ImpDouble b_sum = sum(b1);

    const Vec &sa1 = (f1 < fu)? sa:sb;

    const ImpLong block_size = G.size();
    const ImpInt nr_threads = param->nr_threads;
    Vec G_(nr_threads*block_size, 0);

    const ImpDouble *qp = Q1.data();
    
    if(param->freq){
        vector<vector<ImpLong>> &freq = U1->freq;
        const ImpLong df1 = U1->Ds[fi];
        assert( df1 == freq.size());
        for(ImpInt i = 0; i < df1; i++)
            axpy( W1.data()+i*k, G.data()+i*k, k, lambda * ImpDouble(freq[fi][i]) );
    }
    else{
        axpy( W1.data(), G.data(), G.size(), lambda);
    }

    if(param->freq){
        const vector<ImpLong> &freq = U1->freq[fi];
        const ImpLong df1 = U1->Ds[fi];
        assert( df1 == freq.size());
        for(ImpLong i = 0; i < df1; i++)
            axpy( W1.data()+i*k, G.data()+i*k, k, lambda * ImpDouble(freq[i]));
    }
    else{
        axpy( W1.data(), G.data(), G.size(), lambda);
    }

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < m1; i++) {
        const ImpInt id = omp_get_thread_num();
        const ImpDouble *q1 = qp+i*k;
        ImpDouble z_i = w*(n1*(a1[i]-r)+b_sum+sa1[i]);
        for (Node* y = Y[i]; y < Y[i+1]; y++) {
            const ImpDouble y_tilde = y->val;
            z_i += (1-w)*y_tilde-w*(1-r);
        }
        for (Node* x = X[i]; x < X[i+1]; x++) {
            const ImpLong idx = x->idx;
            const ImpDouble val = x->val;
            for (ImpInt d = 0; d < k; d++) {
                const ImpLong jd = idx*k+d;
                G_[jd+id*block_size] += q1[d]*val*z_i;
            }
        }
    }
    for(ImpInt i = 0; i < nr_threads; i++)
        axpy(G_.data()+i*block_size, G.data(), block_size, 1);
}

void ImpProblem::hs_side(const ImpLong &m1, const ImpLong &n1,
        const Vec &V, Vec &Hv, const Vec &Q1, const vector<Node*> &UX,
        const vector<Node*> &Y, Vec &Hv_) {

    const ImpDouble *qp = Q1.data();
    const ImpInt nr_threads = param->nr_threads;

    const ImpLong block_size = Hv.size();

    #pragma omp parallel for schedule(guided)
        for (ImpLong i = 0; i < m1; i++) {
            ImpInt id = omp_get_thread_num();
            const ImpDouble* q1 = qp+i*k;
            ImpDouble d_1 = (1-w)*ImpInt(Y[i+1] - Y[i]) + w*n1;
            ImpDouble z_1 = 0;
            for (Node* x = UX[i]; x < UX[i+1]; x++) {
                const ImpLong idx = x->idx;
                const ImpDouble val = x->val;
                for (ImpInt d = 0; d < k; d++)
                    z_1 += q1[d]*val*V[idx*k+d];
            }
            z_1 *= d_1;
            for (Node* x = UX[i]; x < UX[i+1]; x++) {
                const ImpLong idx = x->idx;
                const ImpDouble val = x->val;
                for (ImpInt d = 0; d < k; d++) {
                    const ImpLong jd = idx*k+d;
                    Hv_[jd+block_size*id] += q1[d]*val*z_1;
                }
            }
        }

    for(ImpInt i = 0; i < nr_threads; i++)
        axpy(Hv_.data()+i*block_size, Hv.data(), block_size, 1);
}

void ImpProblem::gd_cross(const ImpInt &f1, const ImpInt &f12, const Vec &Q1, const Vec &W1,Vec &G) {


    const Vec &a1 = (f1 < fu)? a: b;
    const Vec &b1 = (f1 < fu)? b: a;

    const vector<Vec> &Ps = (f1 < fu)? P:Q;
    const vector<Vec> &Qs = (f1 < fu)? Q:P;

    const ImpLong &m1 = (f1 < fu)? m:n;
    const ImpLong &n1 = (f1 < fu)? n:m;

    const shared_ptr<ImpData> U1 = (f1 < fu)? U:V;
    const ImpInt fi = (f1 < fu)? f1 : f1 - fu;
    const vector<Node*> &X = U1->Xs[fi];
    const vector<Node*> &Y = U1->Y;

    if(param->freq){
        vector<ImpLong> &freq = U1->freq[fi];
        const ImpLong df1 = U1->Ds[fi];
        assert( df1 == freq.size());
        for(ImpLong i = 0; i < df1; i++)
            axpy( W1.data()+i*k, G.data()+i*k, k, lambda * ImpDouble(freq[i]));
    }
    else{
        axpy( W1.data(), G.data(), G.size(), lambda);
    }

    Vec QTQ(k*k, 0), T(m1*k, 0), o1(n1, 1), oQ(k, 0), bQ(k, 0);

    mv(Q1.data(), o1.data(), oQ.data(), n1, k, 0, true);
    mv(Q1.data(), b1.data(), bQ.data(), n1, k, 0, true);

    for (ImpInt al = 0; al < fu; al++) {
        for (ImpInt be = fu; be < f; be++) {
            const ImpInt fab = index_vec(al, be, f);
            const Vec &Qa = Qs[fab], &Pa = Ps[fab];
            mm(Qa.data(), Q1.data(), QTQ.data(), k, n1);
            mm(Pa.data(), QTQ.data(), T.data(), m1, k, k, 1);
        }
    }

    const ImpLong block_size = G.size();
    const ImpInt nr_threads = param->nr_threads;
    Vec G_(nr_threads*block_size, 0);

    const ImpDouble *tp = T.data(), *qp = Q1.data();

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < m1; i++) {
        Vec pk(k, 0);
        const ImpInt id = omp_get_thread_num();
        const ImpDouble *t1 = tp+i*k;
        for (Node* y = Y[i]; y < Y[i+1]; y++) {
            const ImpDouble scale = (1-w)*y->val-w*(1-r);
            const ImpLong j = y->idx;
            const ImpDouble *q1 = qp+j*k;
            for (ImpInt d = 0; d < k; d++)
                pk[d] += scale*q1[d];
        }

        const ImpDouble z_i = a1[i]-r;
        for (Node* x = X[i]; x < X[i+1]; x++) {
            const ImpLong idx = x->idx;
            const ImpDouble val = x->val;
            for (ImpInt d = 0; d < k; d++) {
                const ImpLong jd = idx*k+d;
                G_[jd+id*block_size] += (pk[d]+w*(t1[d]+z_i*oQ[d]+bQ[d]))*val;
            }
        }
    }
    for(ImpInt i = 0; i < nr_threads; i++)
        axpy(G_.data()+i*block_size, G.data(), block_size, 1);
}


void ImpProblem::hs_cross(const ImpLong &m1, const ImpLong &n1, const Vec &V,
        const Vec &VQTQ, Vec &Hv, const Vec &Q1,
        const vector<Node*> &X, const vector<Node*> &Y, Vec &Hv_) {

    const ImpDouble *qp = Q1.data();

    const ImpLong block_size = Hv.size();
    const ImpInt nr_threads = param->nr_threads;

    #pragma omp parallel for schedule(guided)
        for (ImpLong i = 0; i < m1; i++) {
            const ImpInt id = omp_get_thread_num();
            Vec tau(k, 0), phi(k, 0), ka(k, 0);
            UTx(X[i], X[i+1], V, phi.data());
            UTx(X[i], X[i+1], VQTQ, tau.data());

            for (Node* y = Y[i]; y < Y[i+1]; y++) {
                const ImpLong idx = y->idx;
                const ImpDouble *dp = qp + idx*k;
                const ImpDouble val = inner(phi.data(), dp, k);
                for (ImpInt d = 0; d < k; d++)
                    ka[d] += val*dp[d];
            }

            for (Node* x = X[i]; x < X[i+1]; x++) {
                const ImpLong idx = x->idx;
                const ImpDouble val = x->val;
                for (ImpInt d = 0; d < k; d++) {
                    const ImpLong jd = idx*k+d;
                    Hv_[jd+id*block_size] += ((1-w)*ka[d]+w*tau[d])*val;
                }
            }
        }

    for(ImpInt i = 0; i < nr_threads; i++)
        axpy(Hv_.data()+i*block_size, Hv.data(), block_size, 1);
}

void ImpProblem::cg(const ImpInt &f1, const ImpInt &f2, Vec &S1,
        const Vec &Q1, const Vec &G, Vec &P1) {

    const ImpInt base = (f1 < fu)? 0: fu;
    const ImpInt fi = f1-base;

    const shared_ptr<ImpData> U1 = (f1 < fu)? U:V;
    const vector<Node*> &Y = U1->Y;
    const vector<Node*> &X = U1->Xs[fi];

    const ImpLong m1 = (f1 < fu)? m:n;
    const ImpLong n1 = (f1 < fu)? n:m;

    const ImpLong Df1 = U1->Ds[fi], Df1k = Df1*k;
    const ImpInt nr_threads = param->nr_threads;
    Vec Hv_(nr_threads*Df1k);

    ImpInt nr_cg = 0, max_cg = 20;
    ImpDouble g2 = 0, r2, cg_eps = 1e-4, alpha = 0, beta = 0, gamma = 0, vHv;

    Vec V(Df1k, 0), R(Df1k, 0), Hv(Df1k, 0);
    Vec QTQ, VQTQ;

    if (!(f1 < fu && f2 < fu) && !(f1>=fu && f2>=fu)) {
        QTQ.resize(k*k, 0);
        VQTQ.resize(Df1k, 0);
        mm(Q1.data(), Q1.data(), QTQ.data(), k, n1);
    }

    for (ImpLong jd = 0; jd < Df1k; jd++) {
        R[jd] = -G[jd];
        V[jd] = R[jd];
        g2 += G[jd]*G[jd];
    }

    r2 = g2;
    while (g2*cg_eps < r2 && nr_cg < max_cg) {
        nr_cg++;

        fill(Hv.begin(), Hv.end(), 0);
        fill(Hv_.begin(), Hv_.end(), 0);

        if(param->freq){
            vector<ImpLong> &freq = U1->freq[fi];
            assert( Df1 == freq.size());
            for(ImpLong i = 0; i < Df1; i++)
                axpy( V.data()+i*k, Hv.data()+i*k, k, lambda * ImpDouble(freq[i]));
        }
        else{
            axpy( V.data(), Hv.data(), V.size(), lambda);
        }

        if ((f1 < fu && f2 < fu) || (f1>=fu && f2>=fu))
            hs_side(m1, n1, V, Hv, Q1, X, Y, Hv_);
        else {
            mm(V.data(), QTQ.data(), VQTQ.data(), Df1, k, k);
            hs_cross(m1, n1, V, VQTQ, Hv, Q1, X, Y, Hv_);
        }

        vHv = inner(V.data(), Hv.data(), Df1k);
        gamma = r2;
        alpha = gamma/vHv;
        axpy(V.data(), S1.data(), Df1k, alpha);
        axpy(Hv.data(), R.data(), Df1k, -alpha);
        r2 = inner(R.data(), R.data(), Df1k);
        beta = r2/gamma;
        scal(V.data(), Df1k, beta);
        axpy(R.data(), V.data(), Df1k, 1);
    }
}

void ImpProblem::solve_side(const ImpInt &f1, const ImpInt &f2) {
    const ImpInt f12 = index_vec(f1, f2, f);
    const bool sub_type = (f1 < fu)? 1 : 0;
    const shared_ptr<ImpData> X12 = (sub_type)? U : V;
    const ImpInt base = (sub_type)? 0 : fu;
    const vector<Node*> &U1 = X12->Xs[f1-base], &U2 = X12->Xs[f2-base];
    Vec &W1 = W[f12], &H1 = H[f12], &P1 = P[f12], &Q1 = Q[f12];

    Vec G1(W1.size(), 0), G2(H1.size(), 0);
    Vec S1(W1.size(), 0), S2(H1.size(), 0);

    gd_side(f1, W1, Q1, G1);
    cg(f1, f2, S1, Q1, G1, P1);
    update_side(sub_type, S1, Q1, W1, U1, P1);

    gd_side(f2, H1, P1, G2);
    cg(f2, f1, S2, P1, G2, Q1);
    update_side(sub_type, S2, P1, H1, U2, Q1);
}

void ImpProblem::solve_cross(const ImpInt &f1, const ImpInt &f2) {
    const ImpInt f12 = index_vec(f1, f2, f);
    const vector<Node*> &U1 = U->Xs[f1], &V1 = V->Xs[f2-fu];
    Vec &W1 = W[f12], &H1 = H[f12], &P1 = P[f12], &Q1 = Q[f12];

    Vec GW(W1.size()), GH(H1.size());
    Vec SW(W1.size()), SH(H1.size());

    gd_cross(f1, f12, Q1, W1, GW);
    cg(f1, f2, SW, Q1, GW, P1);
    update_cross(true, SW, Q1, W1, U1, P1);

    gd_cross(f2, f12, P1, H1, GH);
    cg(f2, f1, SH, P1, GH, Q1);
    update_cross(false, SH, P1, H1, V1, Q1);
}

void ImpProblem::one_epoch() {

    if (param->self_side) {
        for (ImpInt f1 = 0; f1 < fu; f1++)
            for (ImpInt f2 = f1; f2 < fu; f2++)
                solve_side(f1, f2);

        for (ImpInt f1 = fu; f1 < f; f1++)
            for (ImpInt f2 = f1; f2 < f; f2++)
                solve_side(f1, f2);
    }

    for (ImpInt f1 = 0; f1 < fu; f1++)
        for (ImpInt f2 = fu; f2 < f; f2++)
            solve_cross(f1, f2);

    if (param->self_side)
        cache_sasb();
}

void ImpProblem::init_va(ImpInt size) {

    if (Uva->file_name.empty())
        return;

    mt = Uva->m;

    const ImpInt nr_blocks = f*(f+1)/2;

    Pva.resize(nr_blocks);
    Qva.resize(nr_blocks);

    for (ImpInt f1 = 0; f1 < f; f1++) {
        const shared_ptr<ImpData> d1 = ((f1<fu)? Uva: V);
        for (ImpInt f2 = f1; f2 < f; f2++) {
            const shared_ptr<ImpData> d2 = ((f2<fu)? Uva: V);
            const ImpInt f12 = index_vec(f1, f2, f);
            Pva[f12].resize(d1->m*k);
            Qva[f12].resize(d2->m*k);
        }
    }

    va_loss.resize(size);
    top_k.resize(size);
    ImpInt start = 5;

    cout << "iter";
    for (ImpInt i = 0; i < size; i++) {
        top_k[i] = start;
        cout.width(12);
        cout << "va_p@" << start;
        start *= 2;
    }
    cout.width(12);
    cout << "ploss";
    cout << endl;
}

void ImpProblem::pred_z(const ImpLong i, ImpDouble *z) {
    for(ImpInt f1 = 0; f1 < fu; f1++) {
        for(ImpInt f2 = fu; f2 < f; f2++) {
            ImpInt f12 = index_vec(f1, f2, f);
            ImpDouble *p1 = Pva[f12].data()+i*k, *q1 = Qva[f12].data();
            mv(q1, p1, z, n, k, 1, false);
        }
    }
}

void ImpProblem::validate() {
    const ImpInt nr_th = param->nr_threads, nr_k = top_k.size();
    ImpLong valid_samples = 0;

    vector<ImpLong> hit_counts(nr_th*nr_k, 0);

    for (ImpInt f1 = 0; f1 < f; f1++) {
        const shared_ptr<ImpData> d1 = ((f1<fu)? Uva: V);
        const ImpInt fi = ((f1>=fu)? f1-fu: f1);

        for (ImpInt f2 = f1; f2 < f; f2++) {
            const shared_ptr<ImpData> d2 = ((f2<fu)? Uva: V);
            const ImpInt fj = ((f2>=fu)? f2-fu: f2);

            const ImpInt f12 = index_vec(f1, f2, f);
            UTX(d1->Xs[fi], d1->m, W[f12], Pva[f12]);
            UTX(d2->Xs[fj], d2->m, H[f12], Qva[f12]);
        }
    }

    Vec at(Uva->m, 0), bt(V->m, 0);

    if (param->self_side) {
        for (ImpInt f1 = 0; f1 < fu; f1++) {
            for (ImpInt f2 = f1; f2 < fu; f2++) {
                const ImpInt f12 = index_vec(f1, f2, f);
                add_side(Pva[f12], Qva[f12], Uva->m, at);
            }
        }
        for (ImpInt f1 = fu; f1 < f; f1++) {
            for (ImpInt f2 = f1; f2 < f; f2++) {
                const ImpInt f12 = index_vec(f1, f2, f);
                add_side(Pva[f12], Qva[f12], V->m, bt);
            }
        }
    }

    ImpDouble ploss = 0;
#pragma omp parallel for schedule(static) reduction(+: valid_samples, ploss)
    for (ImpLong i = 0; i < Uva->m; i++) {
        Vec z(bt);
        pred_z(i, z.data());
        for(Node* y = Uva->Y[i]; y < Uva->Y[i+1]; y++){
            const ImpLong j = y->idx;
            ploss += (1-z[j]-at[i])*(1-z[j]-at[i]);
        }
        prec_k(z.data(), i, top_k, hit_counts);
        valid_samples++;
    }

    loss = sqrt(ploss/Uva->m);

    fill(va_loss.begin(), va_loss.end(), 0);
    for (ImpInt i = 0; i < nr_k; i++) {

        for (ImpLong num_th = 0; num_th < nr_th; num_th++)
            va_loss[i] += hit_counts[i+num_th*nr_k];

        va_loss[i] /= ImpDouble(valid_samples*top_k[i]);
    }
}

void ImpProblem::prec_k(ImpDouble *z, ImpLong i, vector<ImpInt> &top_k, vector<ImpLong> &hit_counts) {
    ImpInt valid_count = 0;
    const ImpInt nr_k = top_k.size();
    vector<ImpLong> hit_count(nr_k, 0);

    ImpInt num_th = omp_get_thread_num();

#ifdef EBUG
    cout << i << ":";
#endif
    for (ImpInt state = 0; state < nr_k; state++) {
        while(valid_count < top_k[state]) {
            ImpLong argmax = distance(z, max_element(z, z+n));
#ifdef EBUG
            cout << argmax << " ";
#endif
            z[argmax] = MIN_Z;
            for (Node* nd = Uva->Y[i]; nd < Uva->Y[i+1]; nd++) {
                if (argmax == nd->idx) {
                    hit_count[state]++;
                    break;
                }
            }
            valid_count++;
        }
    }

#ifdef EBUG
    cout << endl;
#endif
    for (ImpInt i = 1; i < nr_k; i++) {
        hit_count[i] += hit_count[i-1];
    }
    for (ImpInt i = 0; i < nr_k; i++) {
        hit_counts[i+num_th*nr_k] += hit_count[i];
    }
}

void ImpProblem::print_epoch_info(ImpInt t) {
    ImpInt nr_k = top_k.size();
    cout.width(2);
    cout << t+1;
    if (!Uva->file_name.empty()) {
        for (ImpInt i = 0; i < nr_k; i++ ) {
            cout.width(13);
            cout << setprecision(3) << va_loss[i]*100;
        }
        cout.width(13);
        cout << setprecision(3) << loss;
    }
    cout << endl;
}

void ImpProblem::solve() {
    init_va(5);
    for (ImpInt iter = 0; iter < param->nr_pass; iter++) {
        one_epoch();
        if (!Uva->file_name.empty() && iter % 5 == 0) {
            validate();
            print_epoch_info(iter);
        }
    }
}

ImpDouble ImpProblem::pq(const ImpInt &i, const ImpInt &j,const ImpInt &f1, const ImpInt &f2) {
    ImpInt f12 = index_vec(f1, f2, f);
    ImpInt Pi = (f1 < fu)? i : j;
    ImpInt Qj = (f2 < fu)? i : j;
    ImpDouble  *pp = P[f12].data()+Pi*k, *qp = Q[f12].data()+Qj*k;
    return inner(qp, pp, k);
}

ImpDouble ImpProblem::norm_block(const ImpInt &f1,const ImpInt &f2) {
    ImpInt f12 = index_vec(f1, f2, f);
    Vec &W1 = W[f12], H1 = H[f12];
    ImpDouble res = 0;
    res += inner(W1.data(), W1.data(), W1.size());
    res += inner(H1.data(), H1.data(), H1.size());
    return res;
}


ImpDouble ImpProblem::func() {
    ImpDouble res = 0;
    for (ImpInt i = 0; i < m; i++) {
        for(ImpInt j = 0; j < n; j++){
            ImpDouble y_hat = 0;
            for(ImpInt f1 = 0; f1 < f; f1++){
                for(ImpInt f2 = f1; f2 < f; f2++){
                    y_hat += pq(i, j, f1, f2);
                }
            }
            bool pos_term = false;
            for(Node* y = U->Y[i]; y < U->Y[i+1]; y++){
                if ( y->idx == j ) {
                    pos_term = true;
                    break;
                }
            }
            if( pos_term )
                res += (1 - y_hat) * (1 - y_hat);
            else
                res += w * (r - y_hat) * (r - y_hat);
        }
    }

    for(ImpInt f1 = 0; f1 < f; f1++) {
        for(ImpInt f2 = f1; f2 < f; f2++){
            res += lambda*norm_block(f1, f2);
        }
    }
    return 0.5*res;
}


