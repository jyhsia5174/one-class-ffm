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
        const ImpLong k, const ImpLong l) {
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
    return cblas_ddot(k, p, 1, q, 1);
}

void row_wise_inner(const Vec &V1, const Vec &V2, const ImpLong &row,
        const ImpLong &col,const ImpDouble &alpha, Vec &vv){
    const ImpDouble *v1p = V1.data(), *v2p = V2.data();

    #pragma omp parallel for schedule(guided)
    for(ImpInt i = 0; i < row; i++)
        vv[i] += alpha*inner(v1p+i*col, v2p+i*col, col);
}

void init_mat(Vec &vec, const ImpLong nr_rows, const ImpLong nr_cols) {
    default_random_engine ENGINE(rand());
    vec.resize(nr_rows*nr_cols, 0.1);
    uniform_real_distribution<ImpDouble> dist(-0.1*qrsqrt(nr_cols), 0.1*qrsqrt(nr_cols));

    auto gen = std::bind(dist, ENGINE);
    generate(vec.begin(), vec.end(), gen);
}

void ImpData::read(bool has_label, const ImpLong *ds) {
    ifstream fs(file_name);
    string line, label_block, label_str, pos_str, neg_str;
    char dummy;

    ImpLong fid, idx, y_pos_nnz=0, y_neg_nnz=0, x_nnz=0;
    ImpDouble val;

    while (getline(fs, line)) {
        m++;
        istringstream iss(line);

        if (has_label) {
            iss >> label_block;
            istringstream labelst(label_block);
            getline(labelst, pos_str, '|');
            istringstream posst(pos_str);
            while (getline(posst, label_str, ',')) {
                idx = stoi(label_str);
                n = max(n, idx+1);
                y_pos_nnz++;
            }
            getline(labelst, neg_str, '|');
            istringstream negst(neg_str);
            while( getline(negst, label_str, ',')){
                idx = stoi(label_str);
                n = max(n, idx+1);
                y_neg_nnz++;
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
    Ypos.resize(m+1);
    Yneg.resize(m+1);

    if (has_label) {
        nnz_y_pos = y_pos_nnz;
        nnz_y_neg = y_neg_nnz;
        Mpos.resize(y_pos_nnz);
        Mneg.resize(y_neg_nnz);
        popular.resize(n);
        fill(popular.begin(), popular.end(), 0);
    }

    nnx.resize(m);
    nny_pos.resize(m);
    nny_neg.resize(m);
    fill(nnx.begin(), nnx.end(), 0);
    fill(nny_pos.begin(), nny_pos.end(), 0);
    fill(nny_neg.begin(), nny_neg.end(), 0);

    ImpLong nnz_i=0, nnz_j_pos=0, nnz_j_neg=0, i=0;

    while (getline(fs, line)) {
        istringstream iss(line);

        if (has_label) {
            iss >> label_block;
            istringstream labelst(label_block);
            getline(labelst, pos_str, '|');
            istringstream posst(pos_str);
            while (getline(posst, label_str, ',')) {
                nnz_j_pos++;
                ImpLong idx = stoi(label_str);
                Mpos[nnz_j_pos-1].idx = idx;
                popular[idx] += 1;
            }
            nny_pos[i] = nnz_j_pos;
            getline(labelst, neg_str, '|');
            istringstream negst(neg_str);
            while (getline(negst, label_str, ',')) {
                nnz_j_neg++;
                ImpLong idx = stoi(label_str);
                Mneg[nnz_j_neg-1].idx = idx;
                popular[idx] += 1;
            }
            nny_neg[i] = nnz_j_neg;
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
        Ypos[0] = Mpos.data();
        for (ImpLong i = 0; i < m; i++) {
            Ypos[i+1] = Mpos.data() + nny_pos[i];
        }
        Yneg[0] = Mneg.data();
        for (ImpLong i = 0; i < m ; i++){
            Yneg[i+1] = Mneg.data() + nny_neg[i];
        }
    }

    ImpDouble sum = 0;
    for (auto &n : popular)
        sum += n;
    for (auto &n : popular)
        n /= sum;

    for (ImpLong i = m-1; i > 0; i--)
        nnx[i] -= nnx[i-1];
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

void ImpData::transY(const vector<Node*> &YT_pos, const vector<Node*> &YT_neg) {
    n = max(YT_pos.size() - 1, YT_neg.size() - 1 );
    vector<pair<ImpLong, Node*>> perm_pos;
    vector<pair<ImpLong, Node*>> perm_neg;
    ImpLong nnz_pos = 0;
    ImpLong nnz_neg = 0;
    vector<ImpLong> nnzs_pos(m, 0);
    vector<ImpLong> nnzs_neg(m, 0);

    for (ImpLong i = 0; i < n; i++){
        for (Node* y = YT_pos[i]; y < YT_pos[i+1]; y++) {
            if (y->idx >= m )
              continue;
            nnzs_pos[y->idx]++;
            perm_pos.emplace_back(i, y);
            nnz_pos++;
        }
        
        for (Node* y = YT_neg[i]; y < YT_neg[i+1]; y++) {
            if (y->idx >= m )
              continue;
            nnzs_neg[y->idx]++;
            perm_neg.emplace_back(i, y);
            nnz_pos++;
        }
    }

    auto sort_by_column = [&] (const pair<ImpLong, Node*> &lhs,
            const pair<ImpLong, Node*> &rhs) {
        return tie(lhs.second->idx, lhs.first) < tie(rhs.second->idx, rhs.first);
    };

    sort(perm_pos.begin(), perm_pos.end(), sort_by_column);
    sort(perm_neg.begin(), perm_neg.end(), sort_by_column);

    Mpos.resize(nnz_pos);
    nnz_y_pos = nnz_pos;
    nnz_y_neg = nnz_neg;
    for (ImpLong nnz_i = 0; nnz_i < nnz_pos; nnz_i++) {
        Mpos[nnz_i].idx = perm_pos[nnz_i].first;
        Mpos[nnz_i].val = perm_pos[nnz_i].second->val;
    }
    for (ImpLong nnz_i = 0; nnz_i < nnz_neg; nnz_i++) {
        Mneg[nnz_i].idx = perm_neg[nnz_i].first;
        Mneg[nnz_i].val = perm_neg[nnz_i].second->val;
    }

    Ypos[0] = Mpos.data();
    ImpLong start_idx = 0;
    for (ImpLong i = 0; i < m; i++) {
        start_idx += nnzs_pos[i];
        Ypos[i+1] = Mpos.data()+start_idx;
    }

    Yneg[0] = Mneg.data();
    start_idx = 0;
    for (ImpLong i = 0; i < m; i++) {
        start_idx += nnzs_neg[i];
        Yneg[i+1] = Mneg.data()+start_idx;
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
        for (Node* y = U->Ypos[i]; y < U->Ypos[i+1]; y++) {
            ImpLong j = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j);
        }
        for (Node* y = U->Yneg[i]; y < U->Yneg[i+1]; y++) {
            ImpLong j = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j);
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < n; j++) {
        for (Node* y = V->Ypos[j]; y < V->Ypos[j+1]; y++) {
            ImpLong i = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j);
        }
        for (Node* y = V->Yneg[j]; y < V->Yneg[j+1]; y++) {
            ImpLong i = y->idx;
            y->val = a[i]+b[j]+calc_cross(i, j);
        }
    }
}

ImpDouble lr_loss(ImpDouble y_hat, ImpDouble y){
    ImpDouble z = y_hat * y;
    ImpDouble loss = 0;
    if( -z > 0)
        loss = -z + log( 1 + exp(z));
    else
        loss = log(1 + exp(-z));
    return loss;
}

void ImpProblem::line_search_side(const bool &sub_type, Vec &S, 
         const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1, const Vec &G) {
    const ImpLong m1 = (sub_type)? m : n;
    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    shared_ptr<ImpData> V1 = (sub_type)? V:U;

    Vec gaps(m1, 0);
    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);
    row_wise_inner(XS, Q1, m1, k, 1, gaps);
    ImpDouble gTs = inner(G.data(), S.data(), S.size());
    ImpDouble sTs = inner(S.data(), S.data(), S.size());
    ImpDouble wTs = inner(W1.data(), S.data(), S.size());
    ImpDouble theta = 1, beta = 0.1, mu = 0.5;
    while(true){
        ImpDouble func_diff =  lambda / 2 * (2 * theta * wTs + theta * theta * sTs);
        for(ImpLong i = 0; i < U1->m; i++){
            for(Node* y = U1->Ypos[i]; y != U1->Ypos[i+1]; y++)
                func_diff += lr_loss(y->val + gaps[i], 1.0) - lr_loss(y->val, 1.0);
            for(Node* y = U1->Yneg[i]; y != U1->Yneg[i+1]; y++)
                func_diff += lr_loss(y->val + gaps[i], -1.0) - lr_loss(y->val, -1.0);
        }
        
        if(func_diff <= theta * mu * gTs)
            break;
        theta *= beta;
    }
    
    scal( S.data(), S.size(), theta);
}


void ImpProblem::update_side(const bool &sub_type, const Vec &S
        , const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1) {

    const ImpLong m1 = (sub_type)? m : n;
    // Update W1
    axpy( S.data(), W1.data(), S.size(), 1);

    // Update y_tilde and pq
    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    shared_ptr<ImpData> V1 = (sub_type)? V:U;

    Vec gaps(m1, 0);
    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);
    axpy( XS.data(), P1.data(), XS.size(), 1);
    row_wise_inner(XS, Q1, m1, k, 1, gaps);

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < U1->m; i++) {
        for (Node* y = U1->Ypos[i]; y < U1->Ypos[i+1]; y++) {
            y->val += gaps[i];
        }
        for (Node* y = U1->Yneg[i]; y < U1->Yneg[i+1]; y++) {
            y->val += gaps[i];
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < V1->m; j++) {
        for (Node* y = V1->Ypos[j]; y < V1->Ypos[j+1]; y++) {
            const ImpLong i = y->idx;
            y->val += gaps[i];
        }
        for (Node* y = V1->Yneg[j]; y < V1->Yneg[j+1]; y++) {
            const ImpLong i = y->idx;
            y->val += gaps[i];
        }
    }
}

void ImpProblem::line_search_cross(const bool &sub_type, Vec &S, 
         const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1, const Vec &G) {
    const ImpLong m1 = (sub_type)? m : n;
    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    
    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);

    Vec delta_pos;
    Vec delta_neg;
    for(ImpLong i = 0; i < U1->m; i++){
        ImpDouble *XSp =  XS.data() + i * k;
        for(Node* y=U1->Ypos[i]; y != U1->Ypos[i+1]; y++){
            ImpLong idx = y->idx;
            ImpDouble *P1p = P1.data() + idx * k;
            delta_pos.emplace_back(inner(XSp, P1p, k));
        }
        for(Node* y=U1->Yneg[i]; y != U1->Yneg[i+1]; y++){
            ImpLong idx = y->idx;
            ImpDouble *P1p = P1.data() + idx * k;
            delta_neg.emplace_back(inner(XSp, P1p, k));
        }
    }
    
    ImpDouble gTs = inner(G.data(), S.data(), S.size());
    ImpDouble sTs = inner(S.data(), S.data(), S.size());
    ImpDouble wTs = inner(W1.data(), S.data(), S.size());
    ImpDouble theta = 1, beta = 0.1, mu = 0.5;
    while(true){
        ImpDouble func_diff =  lambda / 2 * (2 * theta * wTs + theta * theta * sTs);
        ImpLong pos_idx=0, neg_idx=0;
        for(ImpLong i = 0; i < U1->m; i++){
            for(Node* y = U1->Ypos[i]; y != U1->Ypos[i+1]; y++){
                func_diff += lr_loss(y->val + theta * delta_pos[pos_idx], 1.0) - lr_loss(y->val, 1.0);
                pos_idx++;
            }
            for(Node* y = U1->Yneg[i]; y != U1->Yneg[i+1]; y++){
                func_diff += lr_loss(y->val + theta * delta_neg[i], -1.0) - lr_loss(y->val, -1.0);
                neg_idx++;
            }
        }
        
        if(func_diff <= theta * mu * gTs)
            break;
        theta *= beta;
    }
    
    scal( S.data(), S.size(), theta);
}

void ImpProblem::update_cross(const bool &sub_type, const Vec &S,
        const Vec &Q1, Vec &W1, const vector<Node*> &X12, Vec &P1) {
    axpy( S.data(), W1.data(), S.size(), 1);
    const ImpLong m1 = (sub_type)? m : n;

    shared_ptr<ImpData> U1 = (sub_type)? U:V;
    shared_ptr<ImpData> V1 = (sub_type)? V:U;

    Vec XS(P1.size(), 0);
    UTX(X12, m1, S, XS);
    axpy( XS.data(), P1.data(), P1.size(), 1);

    #pragma omp parallel for schedule(guided)
    for (ImpLong i = 0; i < U1->m; i++) {
        for (Node* y = U1->Ypos[i]; y < U1->Ypos[i+1]; y++) {
            const ImpLong j = y->idx;
            y->val += inner( XS.data()+i*k, Q1.data()+j*k, k);
        }
        for (Node* y = U1->Yneg[i]; y < U1->Yneg[i+1]; y++) {
            const ImpLong j = y->idx;
            y->val += inner( XS.data()+i*k, Q1.data()+j*k, k);
        }
    }
    #pragma omp parallel for schedule(guided)
    for (ImpLong j = 0; j < V1->m; j++) {
        for (Node* y = V1->Ypos[j]; y < V1->Ypos[j+1]; y++) {
            const ImpLong i = y->idx;
            y->val += inner( XS.data()+i*k, Q1.data()+j*k, k);
        }
        for (Node* y = V1->Yneg[j]; y < V1->Yneg[j+1]; y++) {
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
            if(!param->self_side && (f1>=fu || f2<fu))
                continue;
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

double l_fst( double y_hat, double y){
    double z = 1 / (1 + exp( -y_hat * y));
    return (z - 1) * y;
}

double l_snd( double y_hat, double y){
    double z = 1 / (1 + exp( -y_hat * y));
    return z * (1 - z);
}

void ImpProblem::gd_side(const ImpInt &f1, const Vec &W1, const Vec &Q1, Vec &G) {

    const shared_ptr<ImpData> U1 = (f1 < fu)? U:V;
    const vector<Node*> &Ypos = U1->Ypos;
    const vector<Node*> &Yneg = U1->Yneg;

    const ImpInt base = (f1 < fu)? 0: fu;
    const ImpInt fi = f1-base;
    const vector<Node*> &X = U1->Xs[fi];

    const ImpLong m1 = (f1 < fu)? m:n;

    const ImpLong block_size = G.size();
    const ImpInt nr_threads = param->nr_threads;
    Vec G_(nr_threads*block_size, 0);

    const ImpDouble *qp = Q1.data();

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
        double z_i=0;
        for (Node* y = Ypos[i]; y < Ypos[i+1]; y++) {
            const ImpDouble y_tilde = y->val;
            z_i += l_fst(y_tilde, 1.0);
        }
        for (Node* y = Yneg[i]; y < Yneg[i+1]; y++) {
            const ImpDouble y_tilde = y->val;
            z_i += l_fst(y_tilde, -1.0);
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
        const vector<Node*> &Ypos, const vector<Node*> &Yneg, Vec &Hv_) {

    const ImpDouble *qp = Q1.data();
    const ImpInt nr_threads = param->nr_threads;

    const ImpLong block_size = Hv.size();

    #pragma omp parallel for schedule(guided)
        for (ImpLong i = 0; i < m1; i++) {
            ImpInt id = omp_get_thread_num();
            const ImpDouble* q1 = qp+i*k;
            ImpDouble d_1 = 0;
            for(Node* y = Ypos[i]; y < Ypos[i+1]; y++)
                d_1 += l_snd( y->val, 1.0);
            for(Node* y = Yneg[i]; y < Yneg[i+1]; y++)
                d_1 += l_snd( y->val, -1.0);

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
    const vector<Node*> &Ypos = U1->Ypos;

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
        for (Node* y = Ypos[i]; y < Ypos[i+1]; y++) {
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
        Vec &Hv, const Vec &Q1, const vector<Node*> &X, const vector<Node*> &Ypos, 
        const vector<Node*> &Yneg, Vec &Hv_) {

    const ImpDouble *qp = Q1.data();

    const ImpLong block_size = Hv.size();
    const ImpInt nr_threads = param->nr_threads;

    #pragma omp parallel for schedule(guided)
        for (ImpLong i = 0; i < m1; i++) {
            const ImpInt id = omp_get_thread_num();
            Vec phi(k, 0), ka(k, 0);
            UTx(X[i], X[i+1], V, phi.data());

            for (Node* y = Ypos[i]; y < Ypos[i+1]; y++) {
                const ImpLong idx = y->idx;
                const ImpDouble *dp = qp + idx*k;
                const ImpDouble val = l_snd(y->val, 1.0) * inner(phi.data(), dp, k);
                for (ImpInt d = 0; d < k; d++)
                    ka[d] += val*dp[d];
            }
            
            for (Node* y = Yneg[i]; y < Yneg[i+1]; y++) {
                const ImpLong idx = y->idx;
                const ImpDouble *dp = qp + idx*k;
                const ImpDouble val = l_snd(y->val, -1.0) * inner(phi.data(), dp, k);
                for (ImpInt d = 0; d < k; d++)
                    ka[d] += val*dp[d];
            }

            for (Node* x = X[i]; x < X[i+1]; x++) {
                const ImpLong idx = x->idx;
                const ImpDouble val = x->val;
                for (ImpInt d = 0; d < k; d++) {
                    const ImpLong jd = idx*k+d;
                    Hv_[jd+id*block_size] += ka[d]*val;
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
    const vector<Node*> &Ypos = U1->Ypos;
    const vector<Node*> &Yneg = U1->Yneg;
    const vector<Node*> &X = U1->Xs[fi];

    const ImpLong m1 = (f1 < fu)? m:n;
    const ImpLong n1 = (f1 < fu)? n:m;

    const ImpLong Df1 = U1->Ds[fi], Df1k = Df1*k;
    const ImpInt nr_threads = param->nr_threads;
    Vec Hv_(nr_threads*Df1k);

    ImpInt nr_cg = 0, max_cg = 20;
    ImpDouble g2 = 0, r2, cg_eps = 9e-2, alpha = 0, beta = 0, gamma = 0, vHv;

    Vec V(Df1k, 0), R(Df1k, 0), Hv(Df1k, 0);

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
            hs_side(m1, n1, V, Hv, Q1, X, Ypos, Yneg, Hv_);
        else {
            hs_cross(m1, n1, V, Hv, Q1, X, Ypos, Yneg, Hv_);
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
    line_search_side(sub_type, S1, Q1, W1, U1, P1, G1);
    update_side(sub_type, S1, Q1, W1, U1, P1);

    gd_side(f2, H1, P1, G2);
    cg(f2, f1, S2, P1, G2, Q1);
    line_search_side(sub_type, S2, P1, H1, U2, Q1, G2);
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
    line_search_cross(true, SW, Q1, W1, U1, P1, GW);
    update_cross(true, SW, Q1, W1, U1, P1);

    gd_cross(f2, f12, P1, H1, GH);
    cg(f2, f1, SH, P1, GH, Q1);
    line_search_cross(false, SH, P1, H1, V1, Q1, GH);
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
            if(!param->self_side && (f1>=fu || f2<fu))
                continue;
            Pva[f12].resize(d1->m*k);
            Qva[f12].resize(d2->m*k);
        }
    }

    va_loss_prec.resize(size);
    va_loss_ndcg.resize(size);
    top_k.resize(size);
    ImpInt start = 5;

    cout << "iter";
    for (ImpInt i = 0; i < size; i++) {
        top_k[i] = start;
        cout.width(9);
        cout << "( p@ " << start << ", ";
        cout.width(6);
        cout << "nDCG@" << start << " )";
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
    vector<ImpDouble> ndcg_scores(nr_th*nr_k, 0);

    for (ImpInt f1 = 0; f1 < f; f1++) {
        const shared_ptr<ImpData> d1 = ((f1<fu)? Uva: V);
        const ImpInt fi = ((f1>=fu)? f1-fu: f1);

        for (ImpInt f2 = f1; f2 < f; f2++) {
            const shared_ptr<ImpData> d2 = ((f2<fu)? Uva: V);
            const ImpInt fj = ((f2>=fu)? f2-fu: f2);

            const ImpInt f12 = index_vec(f1, f2, f);
            if(!param->self_side && (f1>=fu || f2<fu))
                continue;
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
#ifdef EBUG
    for (ImpLong i = 0; i < n; i++) {
        cout << U->popular[i] << " ";
    }
    cout << endl;
#endif
#pragma omp parallel for schedule(static) reduction(+: valid_samples, ploss)
    for (ImpLong i = 0; i < Uva->m; i++) {
        Vec z, z_copy;
        if(Uva->nnx[i] == 0) {
            z.assign(U->popular.begin(), U->popular.end());
        }
        else {
            z.assign(bt.begin(), bt.end());
            pred_z(i, z.data());
        }
        for(Node* y = Uva->Ypos[i]; y < Uva->Ypos[i+1]; y++){
            const ImpLong j = y->idx;
            if (j < z.size())
                ploss += (1-z[j]-at[i])*(1-z[j]-at[i]);
        }

#ifdef EBUG_nDCG
        z.resize(n);
        z_copy.resize(n);
        for(ImpInt i = 0; i < n ; i++)
          z[i] = z_copy[i] = n - i;
#endif
        z_copy.assign(z.begin(), z.end());
        // Precision @
        prec_k(z.data(), i, hit_counts);
        // nDCG
        ndcg(z_copy.data(), i, ndcg_scores);
        valid_samples++;
    }

    loss = sqrt(ploss/Uva->m);

    fill(va_loss_prec.begin(), va_loss_prec.end(), 0);
    fill(va_loss_ndcg.begin(), va_loss_ndcg.end(), 0);
    for (ImpInt i = 0; i < nr_k; i++) {

        for (ImpLong num_th = 0; num_th < nr_th; num_th++){
            va_loss_prec[i] += hit_counts[i+num_th*nr_k];
            va_loss_ndcg[i] += ndcg_scores[i+num_th*nr_k];
        }

        va_loss_prec[i] /= ImpDouble(valid_samples*top_k[i]);
        va_loss_ndcg[i] /= ImpDouble(valid_samples);
    }
}

void ImpProblem::prec_k(ImpDouble *z, ImpLong i, vector<ImpLong> &hit_counts) {
    ImpInt valid_count = 0;
    const ImpInt nr_k = top_k.size();
    vector<ImpLong> hit_count(nr_k, 0);

    ImpInt num_th = omp_get_thread_num();

#ifdef EBUG
    //cout << i << ":";
#endif
    ImpLong max_z_idx = U->popular.size();
    for (ImpInt state = 0; state < nr_k; state++) {
        while(valid_count < top_k[state]) {
            if ( valid_count >= max_z_idx )
               break;
            ImpLong argmax = distance(z, max_element(z, z + max_z_idx));
#ifdef EBUG
    //        cout << argmax << " ";
#endif
            z[argmax] = MIN_Z;
            for (Node* nd = Uva->Ypos[i]; nd < Uva->Ypos[i+1]; nd++) {
                if (argmax == nd->idx) {
                    hit_count[state]++;
                    break;
                }
            }
            valid_count++;
        }
    }

#ifdef EBUG
    //cout << endl;
#endif
    for (ImpInt i = 1; i < nr_k; i++) {
        hit_count[i] += hit_count[i-1];
    }
    for (ImpInt i = 0; i < nr_k; i++) {
        hit_counts[i+num_th*nr_k] += hit_count[i];
    }
}

void ImpProblem::ndcg(ImpDouble *z, ImpLong i, vector<ImpDouble> &ndcg_scores) {
    ImpInt valid_count = 0;
    const ImpInt nr_k = top_k.size();
    vector<ImpDouble> dcg_score(nr_k, 0);
    vector<ImpDouble> idcg_score(nr_k, 0);

    ImpInt num_th = omp_get_thread_num();

#ifdef EBUG_nDCG
#ifndef SHOW_SCORE_ONLY
    bool show_label = true;
    cout << i << ":";
#endif
#endif
    ImpLong max_z_idx = U->popular.size();
    for (ImpInt state = 0; state < nr_k; state++) {
        while(valid_count < top_k[state]) {
            if ( valid_count >= max_z_idx )
               break;
            ImpLong argmax = distance(z, max_element(z, z + max_z_idx));
#ifdef EBUG_nDCG
#ifndef SHOW_SCORE_ONLY
            if( 10 < top_k[state] )
              break;
            cout << argmax << " ";
#endif
#endif
            z[argmax] = MIN_Z;
#ifdef EBUG_nDCG
#ifndef SHOW_SCORE_ONLY
            if(show_label) {
              cout << "(";
              for (Node* nd = Uva->Ypos[i]; nd < Uva->Ypos[i+1]; nd++)
                  cout << nd->idx << ",";
              cout << ")" << endl;
              show_label = false;
            }
#endif
#endif
            for (Node* nd = Uva->Ypos[i]; nd < Uva->Ypos[i+1]; nd++) {
                if (argmax == nd->idx) {
                    dcg_score[state] += 1.0 / log2(valid_count + 2);
                    break;
                }
            }

            if( ImpInt(Uva->Ypos[i+1] - Uva->Ypos[i]) > valid_count )
                idcg_score[state] += 1.0 / log2(valid_count + 2);
            valid_count++;
        }
    }

#ifdef EBUG_nDCG
#ifndef SHOW_SCORE_ONLY
    cout << endl;
    cout << dcg_score[0] << ", " << idcg_score[0] << endl;
#endif
#endif
    for (ImpInt i = 1; i < nr_k; i++) {
        dcg_score[i] += dcg_score[i-1];
        idcg_score[i] += idcg_score[i-1];
    }

    for (ImpInt i = 0; i < nr_k; i++) {
        ndcg_scores[i+num_th*nr_k] += dcg_score[i] / idcg_score[i];
    }
#ifdef EBUG_nDCG
    cout << setprecision(4) << dcg_score[1] / idcg_score[1] << endl;
#endif
}

void ImpProblem::print_epoch_info(ImpInt t) {
    ImpInt nr_k = top_k.size();
    cout.width(2);
    cout << t+1;
    if (!Uva->file_name.empty()) {
        for (ImpInt i = 0; i < nr_k; i++ ) {
            cout.width(9);
            cout << "( " <<setprecision(3) << va_loss_prec[i]*100 << " ,";
            cout.width(6);
            cout << setprecision(3) << va_loss_ndcg[i]*100 << " )";
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
            if (!Uva->file_name.empty() && iter % 10 == 9) {
                validate();
                print_epoch_info(iter);
            }
    }
}

void ImpProblem::write_header(ofstream &f_out) const{
    f_out << f << endl;
    f_out << fu << endl;
    f_out << fv << endl;
    f_out << k << endl;
    
    for(ImpInt fi = 0; fi < fu ; fi++)
        f_out << U->Ds[fi] << endl;
    
    for(ImpInt fi = 0; fi < fv ; fi++)
        f_out << V->Ds[fi] << endl;
}

void write_block(const Vec& block, const ImpLong& num_of_rows, const ImpInt& num_of_columns, char block_type, const ImpInt fi, const ImpInt fj, ofstream &f_out){
#ifdef DEBUG_SAVE
    if ( block.size() != num_of_columns * num_of_rows ){
        cout << block.size() << " " << num_of_columns << " " << num_of_rows << endl;
        assert(false);
    }
#endif
    ostringstream stringStream;
    stringStream << block_type << ',' << fi << ',' << fj;
    string line_info =  stringStream.str();

    for( ImpLong row_i = 0; row_i < num_of_rows; row_i++ ){
        f_out << line_info << ',' << row_i;
        ImpLong offset = row_i * num_of_columns;
        for(ImpInt col_i = 0; col_i < num_of_columns ; col_i++ ){
            f_out << " " <<block[offset + col_i];
        }
        f_out << endl;
    }
}

void ImpProblem::write_W_and_H(ofstream &f_out) const{
    for(ImpInt fi = 0; fi < f ; fi++){
        for(ImpInt fj = fi; fj < f; fj++){
            ImpInt fij = index_vec(fi, fj, f);
            ImpInt fi_base = (fi >= fu )? fi - fu : fi;
            ImpInt fj_base = (fj >= fu )? fj - fu : fj;
            if ( fi < fu && fj < fu ){
                if( !param->self_side )
                    continue;
                write_block(W[fij], U->Ds[fi_base], k, 'W', fi, fj, f_out);
                write_block(H[fij], U->Ds[fj_base], k, 'H', fi, fj, f_out);
            }
            else if (fi < fu && fj >= fu){
                write_block(W[fij], U->Ds[fi_base], k, 'W', fi, fj, f_out);
                write_block(H[fij], V->Ds[fj_base], k, 'H', fi, fj, f_out);
            }
            else if( fi >= fu && fj >= fu){
                if( !param->self_side )
                    continue;
                write_block(W[fij], V->Ds[fi_base], k, 'W', fi, fj, f_out);
                write_block(H[fij], V->Ds[fj_base], k, 'H', fi, fj, f_out);
            }
        }
    }

}

void save_model(const ImpProblem& prob, string & model_path ){
#ifdef DEBUG_SAVE
    cout << "Start a save\n" << flush;
#endif
    ofstream f_out(model_path, ios::out | ios::trunc );
#ifdef DEBUG_SAVE
    cout << "Success open file.\n" << flush;
#endif
    prob.write_header( f_out );
#ifdef DEBUG_SAVE
    cout << "Success write header.\n" << flush;
#endif
    prob.write_W_and_H( f_out );  
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
            for(Node* y = U->Ypos[i]; y < U->Ypos[i+1]; y++){
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


