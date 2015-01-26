#include "ros/ros.h"

#include "cob_twist_controller/augmented_solver.h"
#include <fstream>

#define DEBUG true


augmented_solver::augmented_solver(const KDL::Chain& _chain, double _eps, int _maxiter):
    chain(_chain),
    jac(chain.getNrOfJoints()),
    jnt2jac(chain),
    maxiter(_maxiter),
    initial_iteration(true),
    last_dh(Eigen::VectorXd::Zero(jac.columns()))
{}

augmented_solver::~augmented_solver()
{}


int augmented_solver::CartToJnt(const KDL::JntArray& q_in, const KDL::JntArray& last_q_dot, KDL::Twist& v_in, KDL::JntArray& qdot_out, std::vector<float> *limits_min, std::vector<float> *limits_max, KDL::Frame &base_position, KDL::Frame &chain_base)
{
    ///used only for debugging
    std::ofstream file("test_end_effect.txt", std::ofstream::app);
    
    ///Let the ChainJntToJacSolver calculate the jacobian "jac_chain" for the current joint positions "q_in"
    KDL::Jacobian jac_chain(chain.getNrOfJoints());
    Eigen::Matrix<double,6,3> jac_b;

    jnt2jac.JntToJac(q_in, jac_chain);
    
    if(params_.base_active)
    {
		Eigen::Matrix<double, 3, 3> chain_base_rot,base_rot,tip_base_rot;
		Eigen::Vector3d w_chain_base;
		Eigen::Vector3d r_chain_base;
		Eigen::Vector3d tangential_vel;
		Eigen::MatrixXd W_base_ratio = Eigen::MatrixXd::Identity(jac.columns(), jac.columns());
		
		Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> prod = jac.data * jac.data.transpose();
		double d = prod.determinant();
		double kappa = std::sqrt(d);
		
		double base_ratio=params_.base_ratio;

        //Create standard platform jacobian
        jac_b.setZero();
        
        // Get current x and y position from EE and chain_base with respect to base_footprint
        x_ = base_position.p.x();
        y_ = base_position.p.y();
		z_ = base_position.p.z();
		Eigen::Vector3d r_base_link(x_,y_,z_);
		
		chain_base_rot << 	chain_base.M.data[0],chain_base.M.data[1],chain_base.M.data[2],
							chain_base.M.data[3],chain_base.M.data[4],chain_base.M.data[5],
							chain_base.M.data[6],chain_base.M.data[7],chain_base.M.data[8];
							
		// Transform from base_link to chain_base
		Eigen::Vector3d w_base_link(0,0,base_ratio);
		//Eigen::Vector3d w_base_link(0,0,1);
		w_chain_base = chain_base_rot*w_base_link;
		r_chain_base = chain_base_rot*r_base_link;
		
		//Calculate tangential velocity
		tangential_vel = w_chain_base.cross(r_chain_base);
		
		 //Vx-Base <==> q8 effects a change in the following chain_base Vx velocities
		jac_b(0,0) = base_ratio*chain_base_rot(0,0);
		jac_b(0,1) = base_ratio*chain_base_rot(0,1);
		jac_b(0,2) = tangential_vel(0);
		
		// Vy-Base <==> q9 effects a change in the following chain_base Vy velocities
		jac_b(1,0) = base_ratio*chain_base_rot(1,0);
		jac_b(1,1) = base_ratio*chain_base_rot(1,1);
		jac_b(1,2) = tangential_vel(1);
		
		// Vz-Base <==>  effects a change in the following chain_base Vz velocities
		jac_b(2,0) = base_ratio*chain_base_rot(2,0);
		jac_b(2,1) = base_ratio*chain_base_rot(2,1);
		jac_b(2,2) = tangential_vel(2);
		
		//Phi <==> Wz with respect to base_link
		jac_b(3,2) = w_chain_base(0);
		jac_b(4,2) = w_chain_base(1);
		jac_b(5,2) = w_chain_base(2);
		
        //combine chain Jacobian and platform Jacobian
        Eigen::Matrix<double, 6, Eigen::Dynamic> jac_full;
        jac_full.resize(6,chain.getNrOfJoints() + jac_b.cols());
        jac_full << jac_chain.data,jac_b;
        
        std::cout << "Combined jacobian:\n " << jac_full << "\n";
        //ROS_INFO_STREAM("JacBase: rows " <<jac_base.rows()<<"; cols "<<jac_base.cols());
        //ROS_INFO_STREAM("JacFull: rows " <<jac_full.rows()<<"; cols "<<jac_full.cols());
        
        //ROS_INFO_STREAM("JacANTE: rows " <<jac.rows()<<"; cols "<<jac.columns());
        jac.resize(chain.getNrOfJoints() + jac_b.cols());
        //ROS_INFO_STREAM("JacPOST: rows " <<jac.rows()<<"; cols "<<jac.columns());
        
        jac.data << jac_full;
    }
    else
    {
        jac.resize(chain.getNrOfJoints());
        jac.data << jac_chain.data;
    }
    
    Eigen::JacobiSVD<Eigen::MatrixXd> svd(jac.data,Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::VectorXd S = svd.singularValues();
    
    double damping_factor = 0.0;
    if (params_.damping_method == MANIPULABILITY)
    {
        Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> prod = jac.data * jac.data.transpose();
        double d = prod.determinant();
        double w = std::sqrt(std::abs(d));
        damping_factor = ((w<params_.wt) ? (params_.lambda0 * pow((1 - w/params_.wt),2)) : 0);
        //std::cout << "w" << w << " wt" <<wt << " Condition" << (bool)(w<wt) << "\n";
        std::cout << "w:" << w << "\n";
    }
    
    else if (params_.damping_method == MANIPULABILITY_RATE)
    {
        Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> prod = jac.data * jac.data.transpose();
        double d = prod.determinant();
        double w = std::sqrt(std::abs(d));
        if (initial_iteration)
        {
            damping_factor = params_.lambda0;
        }
        else
        {
            if(wkm1 == 0) //division by zero
            {    damping_factor = params_.lambda0;    }
            else
            {    damping_factor = ((std::fabs(w/wkm1) > params_.wt) ? (params_.lambda0 * (1-w/wkm1)) : 0);    }
        }
        //std::cout<<"w: "<<w<<" wk-1: "<< wkm1 << " condition:" << (bool)(w/wkm1 <params_.wt) << std::endl;
        wkm1=w;
    }
    
    else if (params_.damping_method == TRACKING_ERROR)
    {
        double min_singular_value = svd.singularValues()(0);
        for (int i=1; i<jac.rows(); i++) 
        {
            if((svd.singularValues()(i) < min_singular_value) && (svd.singularValues()(i)>params_.eps)) //What is a zero singular value, and what is not. Less than 0.005 seems OK
            {
                min_singular_value = svd.singularValues()(i);
                //file << "Minimum sv:"<< min_singular_value << std::endl;
            }
        }
        damping_factor = pow(min_singular_value,2) * params_.deltaRMax/(1-params_.deltaRMax);
    }
    
    else if (params_.damping_method == SINGULAR_REGION)
    {
        damping_factor = ((S(S.size()-1)>=params_.eps) ? 0 : (sqrt(1-pow(S(S.size()-1)/params_.eps,2))*params_.lambda0)); //The last singular value seems to be less than 0.01 near singular configurations
    }
    
    else if (params_.damping_method == CONSTANT)
    {
        damping_factor = params_.damping_factor;
    }
    
    else if (params_.damping_method == TRUNCATION)
    {
        damping_factor = 0.0;
    }
    else
    {
        ROS_ERROR("DampingMethod %d not defined! Aborting!", params_.damping_method);
        return -1;
    }
    
    std::cout << "Daming_factor:" << damping_factor << "\n";

    int task_size = 1;
    
    //Weighting matrix for endeffector Jacobian Je (jac)
    //Eigen::MatrixXd We = Eigen::MatrixXd::Identity(jac.rows(), jac.rows());
    
    //Weighting matrix for additional/task constraints Jc
    //Eigen::MatrixXd Wc = Eigen::MatrixXd::Identity(task_size,task_size);
    //Eigen::MatrixXd Jc = Eigen::MatrixXd::Zero(task_size,jac.columns());
    
    //Weighting matrix for damping 
    Eigen::MatrixXd Wv = Eigen::MatrixXd::Identity(jac.rows(), jac.rows());
    
    Eigen::VectorXd v_in_vec = Eigen::VectorXd::Zero(jac.rows());
    Eigen::VectorXd v_in_vec_base = Eigen::VectorXd::Zero(jac_base.rows());
    Eigen::MatrixXd qdot_out_vec;
    Eigen::MatrixXd qdot_out_vec_enforced;


    ///use calculated damping value lambda for SVD
    Wv = Wv*damping_factor*damping_factor; //why squared?
    
    ///convert input
    for (int i=0; i<jac.rows(); i++)
    {    v_in_vec(i)=v_in(i);    }
    
    ///solution of the equation system    
    if(params_.JLA_active)
    {
        Eigen::MatrixXd W = augmented_solver::calculate_weighting(q_in, last_q_dot, limits_min, limits_max).asDiagonal();
        
        if(params_.damping_method == TRUNCATION)
        {
            Eigen::JacobiSVD<Eigen::MatrixXd> svdWholeMatrix(jac.data*W.inverse()*jac.data.transpose()+Wv,Eigen::ComputeFullU | Eigen::ComputeFullV);
            Eigen::VectorXd SWholeMatrix = svdWholeMatrix.singularValues();
            Eigen::VectorXd S_inv = Eigen::VectorXd::Zero(SWholeMatrix.rows());
            
            for(int i=0;i<SWholeMatrix.rows();i++)
            {    S_inv(i) = SWholeMatrix(i)<params_.eps?0:1/SWholeMatrix(i);    }
            
            Eigen::MatrixXd tmp=svdWholeMatrix.matrixV()*S_inv.asDiagonal()*svdWholeMatrix.matrixV().transpose();
            qdot_out_vec=W.inverse()*jac.data.transpose()*tmp*v_in_vec;
        }
        
        else
        {
            if (jac.columns()>=jac.rows())
            {
                Eigen::MatrixXd tmp = (jac.data*W.inverse()*jac.data.transpose()+Wv).inverse();        
                qdot_out_vec=W.inverse()*jac.data.transpose()*tmp*v_in_vec;
            }
            else   //special case, the last formula is valid only for a full-row Jacobian
            {
                Eigen::MatrixXd Wv_specialcase = Eigen::MatrixXd::Identity(jac.columns(), jac.columns())*damping_factor;
                Eigen::MatrixXd W_specialcase = Eigen::MatrixXd::Identity(jac.columns(), jac.columns());
                for(int i=0;i<jac.columns();i++)
                {
                    W_specialcase(i,i)=sqrt(W(i,i));
                }
                Eigen::MatrixXd tmp = (W_specialcase.inverse()*jac.data.transpose()*jac.data*W_specialcase.inverse()).inverse();
                qdot_out_vec=W_specialcase.inverse()*tmp*W_specialcase.inverse()*jac.data.transpose()*v_in_vec;
            }
        }
    }
    else //JLA is not active
    {
        if(params_.damping_method == TRUNCATION)
        {
            Eigen::MatrixXd jac_pinv = Eigen::MatrixXd::Zero(jac.columns(),jac.rows());
            Eigen::MatrixXd temp = Eigen::MatrixXd::Zero(jac.columns(),jac.rows());
            for (int i=0; i<S.rows(); i++)
            {
                for (int j=0; j<jac.rows(); j++)
                {
                    for (int k=0; k<jac.columns(); k++)
                    {
                        double denominator = pow(S(i),2)+pow(damping_factor,2);
                        double factor = (denominator < params_.eps) ? 0.0 : S(i)/denominator;
                        jac_pinv(k,j)+=factor*svd.matrixV()(k,i)*svd.matrixU()(j,i);
                    }
                }
            }
            qdot_out_vec = jac_pinv*v_in_vec;
        }
        else
        {
            if(jac.columns()>=jac.rows())
            {
                Eigen::MatrixXd tmp = (jac.data*jac.data.transpose()+Wv).inverse();
                qdot_out_vec=jac.data.transpose()*tmp*v_in_vec;
            }
            else  //special case, the last formula is valid only for a full-row Jacobian
            {
                Eigen::MatrixXd Wv_specialcase = Eigen::MatrixXd::Identity(jac.columns(), jac.columns())*damping_factor;
                Eigen::MatrixXd tmp = (jac.data.transpose()*jac.data+Wv_specialcase).inverse();
                qdot_out_vec=tmp*jac.data.transpose()*v_in_vec;
            }
        }
    }
    
    if(params_.enforce_limits)
        {    qdot_out_vec_enforced=augmented_solver::enforce_limits(q_in,&qdot_out_vec,limits_min, limits_max);    }
    else
        {    qdot_out_vec_enforced=qdot_out_vec;    }
    
    ///// formula from book (2.3.19)
    ////reults in oscillation without task constraints and damping close to 0.0 (when far from singularity)?
    //Eigen::MatrixXd tmp = (jac.data.transpose()*We*jac.data+Jc.transpose()*Wc*Jc+Wv).inverse();
    //qdot_out_vec= tmp*(jac.data.transpose()*We*v_in_vec);
    
    //qdot_out_vec= tmp.inverse()*(jac.data.transpose()*We*v_in_vec);
    /// formula from book (2.3.14)
    //additional task constraints can not be considered
    //Eigen::MatrixXd jac_pinv = Eigen::MatrixXd::Zero(jac.columns(),jac.rows());
    //Eigen::MatrixXd temp = Eigen::MatrixXd::Zero(jac.columns(),jac.rows());
    //for (int i=0; i<S.rows(); i++)
    //{
        //for (int j=0; j<jac.rows(); j++)
        //{
            //for (int k=0; k<jac.columns(); k++)
            //{
                //double denominator = pow(S(i),2)+pow(damping_factor,2);
                //double factor = (denominator < params_.eps) ? 0.0 : S(i)/denominator;
                //jac_pinv(k,j)+=factor*svd.matrixV()(k,i)*svd.matrixU()(j,i);
            //}
        //}
    //}
    //qdot_out_vec = jac_pinv*v_in_vec;
    
    ///convert output
    for(int i=0; i<jac.columns(); i++)
    {    qdot_out(i)=qdot_out_vec_enforced(i);    }
    
    
    if(DEBUG)
    {
        //compute manipulability
        ///kappa = sqrt(norm(J*Jt))
        ///see  T.Yoshikawa "Manipulability of robotic mechanisms"
        ///     International Journal of Robotics Research, 4(2):3-9, 1985
        //Eigen::Matrix<double,Eigen::Dynamic,Eigen::Dynamic> prod = jac.data * jac.data.transpose();
        //double d = prod.determinant();
        //double kappa = std::sqrt(d);
        //std::cout << "\nManipulability: " << kappa << "\n";
        //ROS_WARN("Damping factor: %f",damping_factor);
        //std::cout<<"task_size:"<<task_size<<std::endl;
        //std::cout << "Current jacobian:\n " << jac.data << "\n";
        //std::cout << "Current We:\n " << We << "\n";
        //std::cout << "Current Wc:\n " << Wc << "\n";
        //std::cout << "Current Wv:\n " << Wv << "\n";
        //std::cout << "Jc:\n " << Jc << "\n";
        //std::cout<<"Singular values"<<svd.singularValues()<<std::endl;
        //std::cout << "Damping factor" << damping_factor << std::endl;
        //std::cout << "Reciprocal Condition number" << 1/(prod.norm()*prod.inverse().norm())<<'\n';
        //std::cout << "DEBUG END" << std::endl;
        //std::cout << "Output: '\n'" << qdot_out_vec << std::endl;
    }
    
    if (initial_iteration)
    {    initial_iteration=false;    }
    
    return 1;
}

Eigen::VectorXd augmented_solver::calculate_weighting(const KDL::JntArray& q, const KDL::JntArray& last_q_dot, std::vector<float> *limits_min, std::vector<float> *limits_max){
    
    //This function calculates the weighting matrix used to penalize a joint when it is near and moving towards a limit
    //The last joint velocity is used to determine if it that happens or not
    
    Eigen::VectorXd output = Eigen::VectorXd::Zero(jac.columns());
    
    std::vector<float>& limits_min_ = *limits_min;
    std::vector<float>& limits_max_ = *limits_max;
    
    for(int i=0; i<jac.columns() ; i++) {

        if(i<chain.getNrOfJoints()) {    //See Chan paper
            double dh = fabs(pow(limits_max_[i]/M_PI*180-limits_min_[i]/M_PI*180,2)*(2*q(i)/M_PI*180-limits_max_[i]/M_PI*180-limits_min_[i]/M_PI*180)/(4*pow(limits_max_[i]/M_PI*180-q(i)/M_PI*180,2)*pow(q(i)/M_PI*180-limits_min_[i]/M_PI*180,2)));
            //std::cout<<"dh:"<<dh<<std::endl;

            if(initial_iteration)
            {    output(i)=1+dh;    }
            else {    //Penalize a joint only when its moving towards the limit
                if(last_dh(i)==dh || last_q_dot(i)>0 && ((limits_max_[i]-q(i)) < (q(i)-limits_min_[i])) || last_q_dot(i)<0 && ((limits_max_[i]-q(i)) > (q(i)-limits_min_[i])))
                {    output(i) = 1+dh;    }
                else
                {    output(i) = 1;    }
            }
            last_dh(i)=dh;
        }

        else
        {    output(i) = 1;    }
    }

    return output;
}

Eigen::VectorXd augmented_solver::enforce_limits(const KDL::JntArray& q, Eigen::MatrixXd *qdot_out, std::vector<float> *limits_min, std::vector<float> *limits_max) {
    
    //This function multiplies the velocities that result from the IK, in case they violate the specified tolerance
    //This factor uses the cosine function to provide a smooth transition from 1 to zero
    //The factor is not used if the output causes the joint to move far from its limits
    
    Eigen::VectorXd output = Eigen::VectorXd::Zero(jac.columns());
    double tolerance=params_.tolerance/180*M_PI;
    double factor=0;
    bool tolerance_surpassed=false;
    
    
    for(int i=0; i<jac.columns() ;i++) {
        if(i<chain.getNrOfJoints()) {
            if((*limits_max)[i]-q(i)<tolerance) {        //Joint is nearer to the maximum limit
                if((*qdot_out)(i)>0)                     //Joint moves towards the limit
                    {    
                        double temp=1/pow((0.5+0.5*cos(M_PI*(q(i)+tolerance-(*limits_max)[i])/tolerance)),5);
                        factor=temp>factor?temp:factor;
                        tolerance_surpassed=true;
                    }
                //else
                    //{    output(i)=(*qdot_out)(i);    }
            }
            else 
                if(q(i)-(*limits_min)[i]<tolerance) {    //Joint is nearer to the minimum limit
                    if((*qdot_out)(i)<0)                 //Joint moves towards the limit
                        {
                            double temp=1/pow(0.5+0.5*cos(M_PI*(q(i)-tolerance-(*limits_min)[i])/tolerance),5);
                            factor=temp>factor?temp:factor;
                            tolerance_surpassed=true;
                        }
                    //else
                        //{    output(i)=(*qdot_out)(i);    }
                }
                //else
                    //{    output(i)=(*qdot_out)(i);    }
        }
        //else
            //{    output(i)=(*qdot_out)(i);    }
    }
    
    for(int i=0; i<jac.columns() ;i++) {
        output(i)=tolerance_surpassed?(*qdot_out)(i)/factor:(*qdot_out)(i);
    }
    
    return output;
}
