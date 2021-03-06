/*
 libFM: Factorization Machines
 
 Based on the publication(s):
 * Steffen Rendle (2010): Factorization Machines, in Proceedings of the 10th IEEE International Conference on Data Mining (ICDM 2010), Sydney, Australia.
 * Steffen Rendle, Zeno Gantner, Christoph Freudenthaler, Lars Schmidt-Thieme (2011): Fast Context-aware Recommendations with Factorization Machines, in Proceedings of the 34th international ACM SIGIR conference on Research and development in information retrieval (SIGIR 2011), Beijing, China.
 * Christoph Freudenthaler, Lars Schmidt-Thieme, Steffen Rendle (2011): Bayesian Factorization Machines, in NIPS Workshop on Sparse Representation and Low-rank Approximation (NIPS-WS 2011), Spain.
 * Steffen Rendle (2012): Learning Recommender Systems with Adaptive Regularization, in Proceedings of the 5th ACM International Conference on Web Search and Data Mining (WSDM 2012), Seattle, USA.
 * Steffen Rendle (2012): Factorization Machines with libFM, ACM Transactions on Intelligent Systems and Technology (TIST 2012).
 * Steffen Rendle (2013): Scaling Factorization Machines to Relational Data, in Proceedings of the 39th international conference on Very Large Data Bases (VLDB 2013), Trento, Italy.
 
 Author:   Steffen Rendle, http://www.libfm.org/
 modified: 2013-07-12
 
 Copyright 2010-2013 Steffen Rendle, see license.txt for more information
 */

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>
#include <iterator>
#include <algorithm>
#include <iomanip>
#include "../util/util.h"
#include "../util/cmdline.h"
#include "../fm_core/fm_model.h"
#include "src/Data.h"
#include "src/fm_learn.h"
#include "src/fm_learn_sgd.h"
#include "src/fm_learn_sgd_element.h"
#include "src/fm_learn_sgd_element_adapt_reg.h"
#include "src/fm_learn_mcmc_simultaneous.h"


using namespace std;

int main(int argc, char **argv) {
    
    srand ( time(NULL) );
    try {
        CMDLine cmdline(argc, argv);
        std::cout << "----------------------------------------------------------------------------" << std::endl;
        std::cout << "libFM" << std::endl;
        std::cout << "  Version: 1.40" << std::endl;
        std::cout << "  Author:  Steffen Rendle, steffen.rendle@uni-konstanz.de" << std::endl;
        std::cout << "  WWW:     http://www.libfm.org/" << std::endl;
        std::cout << "  License: Free for academic use. See license.txt." << std::endl;
        std::cout << "----------------------------------------------------------------------------" << std::endl;
        
        const std::string param_task		= cmdline.registerParameter("task", "r=regression, c=binary classification [MANDATORY]");
        const std::string param_meta_file	= cmdline.registerParameter("meta", "filename for meta information about data set");
        const std::string param_train_file	= cmdline.registerParameter("train", "filename for training data [MANDATORY]");
        const std::string param_test_file	= cmdline.registerParameter("test", "filename for test data [MANDATORY]");
        const std::string param_val_file	= cmdline.registerParameter("validation", "filename for validation data (only for SGDA)");
        const std::string param_out		= cmdline.registerParameter("out", "filename for output");
        
        const std::string param_dim		= cmdline.registerParameter("dim", "'k0,k1,k2': k0=use bias, k1=use 1-way interactions, k2=dim of 2-way interactions; default=1,1,8");
        const std::string param_regular		= cmdline.registerParameter("regular", "'r0,r1,r2' for SGD and ALS: r0=bias regularization, r1=1-way regularization, r2=2-way regularization");
        const std::string param_init_stdev	= cmdline.registerParameter("init_stdev", "stdev for initialization of 2-way factors; default=0.1");
        const std::string param_num_iter	= cmdline.registerParameter("iter", "number of iterations; default=100");
        const std::string param_learn_rate	= cmdline.registerParameter("learn_rate", "learn_rate for SGD; default=0.1");
        
        const std::string param_method		= cmdline.registerParameter("method", "learning method (SGD, SGDA, ALS, MCMC); default=MCMC");
        
        const std::string param_verbosity	= cmdline.registerParameter("verbosity", "how much infos to print; default=0");
        const std::string param_r_log		= cmdline.registerParameter("rlog", "write measurements within iterations to a file; default=''");
        const std::string param_help            = cmdline.registerParameter("help", "this screen");
        
        const std::string param_relation	= cmdline.registerParameter("relation", "BS: filenames for the relations, default=''");
        
        const std::string param_cache_size = cmdline.registerParameter("cache_size", "cache size for data storage (only applicable if data is in binary format), default=infty");
        
        
        const std::string param_do_sampling	= "do_sampling";
        const std::string param_do_multilevel	= "do_multilevel";
        const std::string param_num_eval_cases  = "num_eval_cases";
        
        if (cmdline.hasParameter(param_help) || (argc == 1)) {
            cmdline.print_help();
            return 0;
        }
        cmdline.checkParameters();
        
        //设置默认值，方法：mcmc α：0.1 维度：k0=use bias, k1=use 1-way interactions, k2=dim of 2-way interactions;
        if (! cmdline.hasParameter(param_method)) { cmdline.setValue(param_method, "mcmc"); }
        if (! cmdline.hasParameter(param_init_stdev)) { cmdline.setValue(param_init_stdev, "0.1"); }
        if (! cmdline.hasParameter(param_dim)) { cmdline.setValue(param_dim, "1,1,8"); }
        
        if (! cmdline.getValue(param_method).compare("als")) { // als is an mcmc without sampling and hyperparameter inference
            cmdline.setValue(param_method, "mcmc");
            if (! cmdline.hasParameter(param_do_sampling)) {
                cmdline.setValue(param_do_sampling, "0");
            }
            if (! cmdline.hasParameter(param_do_multilevel)) {
                cmdline.setValue(param_do_multilevel, "0");
            }
        }
        
        /* (1) Load the data    */
        std::cout << "Loading train...\t" << std::endl;
        //初始化、load数据、开启debug
        //Data(uint64 cache_size, bool has_x, bool has_xt)
        //如果是mcmc，那么has_x = false,has_xt = true
        
        /* 1.1 读入训练数据和test数据   */
        Data train(
                   cmdline.getValue(param_cache_size, 0),
                   ! (!cmdline.getValue(param_method).compare("mcmc")), // no original data for mcmc
                   ! (!cmdline.getValue(param_method).compare("sgd") || !cmdline.getValue(param_method).compare("sgda")) // no transpose data for sgd, sgda
                   );
        train.load(cmdline.getValue(param_train_file));
        if (cmdline.getValue(param_verbosity, 0) > 0) { train.debug(); }
        
        std::cout << "Loading test... \t" << std::endl;
        Data test(
                  cmdline.getValue(param_cache_size, 0),
                  ! (!cmdline.getValue(param_method).compare("mcmc")), // no original data for mcmc
                  ! (!cmdline.getValue(param_method).compare("sgd") || !cmdline.getValue(param_method).compare("sgda")) // no transpose data for sgd, sgda
                  );
        test.load(cmdline.getValue(param_test_file));
        if (cmdline.getValue(param_verbosity, 0) > 0) { test.debug(); }
        
        /* 1.2 读入validation数据（只用于sgda） */
        Data* validation = NULL;
        if (cmdline.hasParameter(param_val_file)) {
            if (cmdline.getValue(param_method).compare("sgda")) {
                std::cout << "WARNING: Validation data is only used for SGDA. The data is ignored." << std::endl;
            } else {
                std::cout << "Loading validation set...\t" << std::endl;
                validation = new Data(
                                      cmdline.getValue(param_cache_size, 0),
                                      ! (!cmdline.getValue(param_method).compare("mcmc")), // no original data for mcmc
                                      ! (!cmdline.getValue(param_method).compare("sgd") || !cmdline.getValue(param_method).compare("sgda")) // no transpose data for sgd, sgda
                                      );
                validation->load(cmdline.getValue(param_val_file));
                if (cmdline.getValue(param_verbosity, 0) > 0) { validation->debug(); }
            }
        }
        
        /* 1.3 读入relational数据 block structure
         一般不用 ，但是用起来会减少运算时间和存储空间*/
        DVector<RelationData*> relation;
        
        vector<std::string> rel = cmdline.getStrValues(param_relation);
        
        std::cout << "#relations: " << rel.size() << std::endl;
        relation.setSize(rel.size());
        train.relation.setSize(rel.size());
        test.relation.setSize(rel.size());
        for (uint i = 0; i < rel.size(); i++) {
            relation(i) = new RelationData(
                                           cmdline.getValue(param_cache_size, 0),
                                           ! (!cmdline.getValue(param_method).compare("mcmc")), // no original data for mcmc
                                           ! (!cmdline.getValue(param_method).compare("sgd") || !cmdline.getValue(param_method).compare("sgda")) // no transpose data for sgd, sgda
                                           );
            relation(i)->load(rel[i]);
            train.relation(i).data = relation(i);
            test.relation(i).data = relation(i);
            train.relation(i).load(rel[i] + ".train", train.num_cases);
            test.relation(i).load(rel[i] + ".test", test.num_cases);
        }
        
        /* 1.4 读入 Load meta data ，其实就是正则项的group */
        std::cout << "Loading meta data...\t" << std::endl;
        
        // (main table)
        uint num_all_attribute = std::max(train.num_feature, test.num_feature);
        if (validation != NULL) {
            num_all_attribute = std::max(num_all_attribute, (uint) validation->num_feature);
        }
        DataMetaInfo meta_main(num_all_attribute);
        if (cmdline.hasParameter(param_meta_file)) {
            meta_main.loadGroupsFromFile(cmdline.getValue(param_meta_file));
        }
        
        // build the joined meta table
        for (uint r = 0; r < train.relation.dim; r++) {
            train.relation(r).data->attr_offset = num_all_attribute;
            num_all_attribute += train.relation(r).data->num_feature;
        }
        DataMetaInfo meta(num_all_attribute);
        
        meta.num_attr_groups = meta_main.num_attr_groups;
        for (uint r = 0; r < relation.dim; r++) {
            meta.num_attr_groups += relation(r)->meta->num_attr_groups;
        }
        meta.num_attr_per_group.setSize(meta.num_attr_groups);
        meta.num_attr_per_group.init(0);//！！初始化为0
        for (uint i = 0; i < meta_main.attr_group.dim; i++) {
            meta.attr_group(i) = meta_main.attr_group(i);
            meta.num_attr_per_group(meta.attr_group(i))++;
        }
        
        uint attr_cntr = meta_main.attr_group.dim;
        uint attr_group_cntr = meta_main.num_attr_groups;
        for (uint r = 0; r < relation.dim; r++) {
            for (uint i = 0; i < relation(r)->meta->attr_group.dim; i++) {
                meta.attr_group(i+attr_cntr) = attr_group_cntr + relation(r)->meta->attr_group(i);
                meta.num_attr_per_group(attr_group_cntr + relation(r)->meta->attr_group(i))++;
            }
            attr_cntr += relation(r)->meta->attr_group.dim;
            attr_group_cntr += relation(r)->meta->num_attr_groups;
        }
        if (cmdline.getValue(param_verbosity, 0) > 0) { meta.debug(); }
        
        
        meta.num_relations = train.relation.dim;
        
        /* (2) Setup the factorization machine */
        fm_model fm;
        
        fm.num_attribute = num_all_attribute;//含有的 feature 数量
        fm.init_stdev = cmdline.getValue(param_init_stdev, 0.1);
        // set the number of dimensions in the factorization
        vector<int> dim = cmdline.getIntValues(param_dim);
        assert(dim.size() == 3);
        fm.k0 = dim[0] != 0;
        fm.k1 = dim[1] != 0;
        fm.num_factor = dim[2];
        
        //初始化,因为这里还不知道是否使用mcmc，是一个通用的初始化
        //w0 = 0
        //w1~wp = 0
        //v<1,1> ~ v<p,k> = ran_gaussian(mean, stdev);
        fm.init();
        
        // (3) Setup the learning method:
        fm_learn* fml;
        if (! cmdline.getValue(param_method).compare("sgd")) {
            fml = new fm_learn_sgd_element();
            ((fm_learn_sgd*)fml)->num_iter = cmdline.getValue(param_num_iter, 100);
            
        } else if (! cmdline.getValue(param_method).compare("sgda")) {
            assert(validation != NULL);
            fml = new fm_learn_sgd_element_adapt_reg();
            ((fm_learn_sgd*)fml)->num_iter = cmdline.getValue(param_num_iter, 100);
            ((fm_learn_sgd_element_adapt_reg*)fml)->validation = validation;
            
        } else if (! cmdline.getValue(param_method).compare("mcmc")) {
            //use mcmc method
            //init w1 ~ wp via N(μ,σ2)
            fm.w.init_normal(fm.init_mean, fm.init_stdev);
            fml = new fm_learn_mcmc_simultaneous(); //fm_learn_mcmc_simultaneous inherits from fm_learn_mcmc
            fml->validation = validation;
            ((fm_learn_mcmc*)fml)->num_iter = cmdline.getValue(param_num_iter, 100);//默认迭代一百次
            ((fm_learn_mcmc*)fml)->num_eval_cases = cmdline.getValue(param_num_eval_cases, test.num_cases);//test instances 的数量
            ((fm_learn_mcmc*)fml)->do_sample = cmdline.getValue(param_do_sampling, true);//do sampling
            ((fm_learn_mcmc*)fml)->do_multilevel = cmdline.getValue(param_do_multilevel, true);//what's this?表示的level-2维度，任意两两interaction
        } else {
            throw "unknown method";
        }
        fml->fm = &fm;//set the fm model
        fml->max_target = train.max_target;
        fml->min_target = train.min_target;
        fml->meta = &meta;
        if (! cmdline.getValue("task").compare("r") ) {//regression
            fml->task = 0;
        } else if (! cmdline.getValue("task").compare("c") ) {//classfication
            fml->task = 1;
            //遍历所有target的值，如果是分类问题，将target < 0.0的设置为1
            for (uint i = 0; i < train.target.dim; i++){
                if (train.target(i) <= 0.0){
                    train.target(i) = -1.0;
                } else{
                    train.target(i) = 1.0;
                }
            }
            for (uint i = 0; i < test.target.dim; i++) {
                if (test.target(i) <= 0.0) {
                    test.target(i) = -1.0;
                } else {
                    test.target(i) = 1.0;
                }
            }
            if (validation != NULL) {
                for (uint i = 0; i < validation->target.dim; i++) {
                    if (validation->target(i) <= 0.0) {
                        validation->target(i) = -1.0;
                    } else {
                        validation->target(i) = 1.0;
                    }
                }
            }
        } else {
            throw "unknown task";
        }
        
        // (4) init the logging
        RLog* rlog = NULL;
        if (cmdline.hasParameter(param_r_log)) {
            ofstream* out_rlog = NULL;
            std::string r_log_str = cmdline.getValue(param_r_log);
            out_rlog = new ofstream(r_log_str.c_str());
            if (! out_rlog->is_open())	{
                throw "Unable to open file " + r_log_str;
            }
            std::cout << "logging to " << r_log_str.c_str() << std::endl;
            rlog = new RLog(out_rlog);
        }
        
        fml->log = rlog;
        fml->init();
        
        // (5) set regularization
        // for als and mcmc this can be individual per group
        if (! cmdline.getValue(param_method).compare("mcmc")) {
            
            vector<double> reg = cmdline.getDblValues(param_regular);
            //if we use individual λ per group,there are 2*group+1 λs
            assert((reg.size() == 0) || (reg.size() == 1) || (reg.size() == 3) || (reg.size() == (1+meta.num_attr_groups*2)));
            
            if (reg.size() == 0) {
                fm.reg0 = 0.0;
                fm.regw = 0.0;
                fm.regv = 0.0;
                ((fm_learn_mcmc*)fml)->w_lambda.init(fm.regw);
                ((fm_learn_mcmc*)fml)->v_lambda.init(fm.regv);
            } else if (reg.size() == 1) {
                fm.reg0 = reg[0];
                fm.regw = reg[0];
                fm.regv = reg[0];
                ((fm_learn_mcmc*)fml)->w_lambda.init(fm.regw);
                ((fm_learn_mcmc*)fml)->v_lambda.init(fm.regv);
            } else if (reg.size() == 3) {
                fm.reg0 = reg[0];
                fm.regw = reg[1];
                fm.regv = reg[2];
                ((fm_learn_mcmc*)fml)->w_lambda.init(fm.regw);
                ((fm_learn_mcmc*)fml)->v_lambda.init(fm.regv);
            } else {
                //individual λ per group
                fm.reg0 = reg[0];
                fm.regw = 0.0;
                fm.regv = 0.0;
                int j = 1;
                for (uint g = 0; g < meta.num_attr_groups; g++) {
                    ((fm_learn_mcmc*)fml)->w_lambda(g) = reg[j];
                    j++;
                }
                for (uint g = 0; g < meta.num_attr_groups; g++) {
                    for (int f = 0; f < fm.num_factor; f++) {
                        ((fm_learn_mcmc*)fml)->v_lambda(g,f) = reg[j];//v<π，f> is a group, means each latent factor vector has a λ
                    }
                    j++;
                }
            }
        } else {
            // 注意：这里SGD是不允许用分组的正则项的
            // set the regularization; for standard SGD, groups are not supported
            //  ’r0,r1,r2’ for SGD and ALS: r0=bias regularization,
            //  r1=1-way regularization, r2=2-way regularization
            
            vector<double> reg = cmdline.getDblValues(param_regular);
            assert((reg.size() == 0) || (reg.size() == 1) || (reg.size() == 3));
            if (reg.size() == 0) {
                fm.reg0 = 0.0;
                fm.regw = 0.0;
                fm.regv = 0.0;
            } else if (reg.size() == 1) {
                fm.reg0 = reg[0];
                fm.regw = reg[0];
                fm.regv = reg[0];
            } else {
                fm.reg0 = reg[0];
                fm.regw = reg[1];
                fm.regv = reg[2];
            }
            
        }
        /*dynamic_cast运算符可以在执行期决定真正的类型。如果downcast是安全的
         （也就说，如果基类指针或者引用确实指向一个派生类对象）
         这个运算符会传回适当转型过的指针。如果downcast不安全，这个运算符会传回空指针*/
        fm_learn_sgd* fmlsgd= dynamic_cast<fm_learn_sgd*>(fml);
        if (fmlsgd) {
            // set the learning rates (individual per layer)
            vector<double> lr = cmdline.getDblValues(param_learn_rate);
            assert((lr.size() == 1) || (lr.size() == 3));
            if (lr.size() == 1) {
                fmlsgd->learn_rate = lr[0];
                fmlsgd->learn_rates.init(lr[0]);
            } else {
                fmlsgd->learn_rate = 0;
                fmlsgd->learn_rates(0) = lr[0];
                fmlsgd->learn_rates(1) = lr[1];
                fmlsgd->learn_rates(2) = lr[2];
            }
            
        }
        
        if (rlog != NULL) {
            rlog->init();
        }
        
        if (cmdline.getValue(param_verbosity, 0) > 0) {
            fm.debug();
            fml->debug();
        }
        
        // () learn，MCMC调用的是fm_learn_mcmc.h
        //如果是mcmc，将使用上文 fml = new fm_learn_mcmc_simultaneous(); //fm_learn_mcmc_simultaneous inherits from fm_learn_mcmc
        fml->learn(train, test);
        
        // () Prediction at the end  (not for mcmc and als)
        if (cmdline.getValue(param_method).compare("mcmc")) {
            std::cout << "Final\t" << "Train=" << fml->evaluate(train) << "\tTest=" << fml->evaluate(test) << std::endl;
        }
        
        // () Save prediction
        if (cmdline.hasParameter(param_out)) {
            DVector<double> pred;
            pred.setSize(test.num_cases);
            fml->predict(test, pred);
            pred.save(cmdline.getValue(param_out));
        }
    } catch (std::string &e) {
        std::cerr << std::endl << "ERROR: " << e << std::endl;
    } catch (char const* &e) {
        std::cerr << std::endl << "ERROR: " << e << std::endl;
    }
}
