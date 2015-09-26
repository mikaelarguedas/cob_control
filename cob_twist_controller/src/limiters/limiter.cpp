/*!
 *****************************************************************
 * \file
 *
 * \note
 *   Copyright (c) 2015 \n
 *   Fraunhofer Institute for Manufacturing Engineering
 *   and Automation (IPA) \n\n
 *
 *****************************************************************
 *
 * \note
 *   Project name: care-o-bot
 * \note
 *   ROS stack name: cob_control
 * \note
 *   ROS package name: cob_twist_controller
 *
 * \author
 *   Author: Marco Bezzon, email: Marco.Bezzon@ipa.fraunhofer.de
 *
 * \date Date of creation: March, 2015
 *
 * \brief
 *   This module contains the implementation of all classes and their
 *   methods to limit joint positions / velocities / accelerations.
 *
 ****************************************************************/
#include <ros/ros.h>
#include <kdl/chain.hpp>

#include "cob_twist_controller/limiters/limiter.h"

/* BEGIN LimiterContainer ***************************************************************************************/
/**
 * This implementation calls enforce limits on all registered Limiters in the limiters vector.
 * The method is based on the last calculation of q_dot.
 */
KDL::JntArray LimiterContainer::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    // If nothing to do just return q_dot.
    KDL::JntArray tmp_q_dots(q_dot_ik);
    for (LimIter_t it = this->limiters_.begin(); it != this->limiters_.end(); it++)
    {
        tmp_q_dots = (*it)->enforceLimits(tmp_q_dots, q);
    }

    return tmp_q_dots;
}

/**
 * Building the limiters vector according the the chosen parameters.
 */
void LimiterContainer::init()
{
    this->eraseAll();

    if(this->tc_params_.keep_direction)
    {
        if(this->tc_params_.enforce_pos_limits)
        {
            this->add(new LimiterAllJointPositions(this->tc_params_, this->chain_));
        }

        if(this->tc_params_.enforce_vel_limits)
        {
            this->add(new LimiterAllJointVelocities(this->tc_params_, this->chain_));
        }
        
        if(this->tc_params_.enforce_acc_limits)
        {
            this->add(new LimiterAllJointAccelerations(this->tc_params_, this->chain_));
        }
    }
    else
    {
        if(this->tc_params_.enforce_pos_limits)
        {
            this->add(new LimiterIndividualJointPositions(this->tc_params_, this->chain_));
        }

        if(this->tc_params_.enforce_vel_limits)
        {
            this->add(new LimiterIndividualJointVelocities(this->tc_params_, this->chain_));
        }
        
        if(this->tc_params_.enforce_acc_limits)
        {
            this->add(new LimiterIndividualJointAccelerations(this->tc_params_, this->chain_));
        }
    }
}

/**
 * Deletes all limiters and clears the vector holding them.
 */
void LimiterContainer::eraseAll()
{
    for (uint32_t cnt = 0; cnt < this->limiters_.capacity(); ++cnt)
    {
        const LimiterBase* lb = this->limiters_[cnt];
        delete(lb);
    }

    this->limiters_.clear();
}

/**
 * Adding new limiters to the vector.
 */
void LimiterContainer::add(const LimiterBase* lb)
{
    this->limiters_.push_back(lb);
}

/**
 * Destruction of the whole container
 */
LimiterContainer::~LimiterContainer()
{
    this->eraseAll();
}
/* END LimiterContainer *****************************************************************************************/

/* BEGIN LimiterAllJointPositions *******************************************************************************/
/**
 * Checks the positions of the joints whether they are in limits_tolerance or not. If not the corresponding velocities vector is scaled.
 * This function multiplies the velocities that result from the IK with a limits-dependent factor in case the joint positions violate the specified limits_tolerance.
 * The factor is calculated by using the cosine function to provide a smooth transition from 1 to zero.
 * Factor is applied on all joint velocities (although only one joint has exceeded its limits), so that the direction of the desired twist is not changed.
 * -> Important for the Use-Case to follow a trajectory exactly!
 */
KDL::JntArray LimiterAllJointPositions::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray scaled_q_dot(q_dot_ik.rows()); // according to KDL: all elements in data have 0 value;
    double tolerance = this->tc_params_.limits_tolerance / 180.0 * M_PI;

    double factor = 0.0;
    bool tolerance_surpassed = false;

    for(int i = 0; i < q_dot_ik.rows() ; ++i)
    {
        if(i < chain_.getNrOfJoints())
        {
            if((this->tc_params_.limits_max[i] - q(i)) < tolerance)    //Joint is nearer to the MAXIMUM limit
            {
                if(q_dot_ik(i) > 0)   //Joint moves towards the MAX limit
                {
                    double temp = 1.0 / pow((0.5 + 0.5 * cos(M_PI * (q(i) + tolerance - this->tc_params_.limits_max[i]) / tolerance)), 5.0);
                    factor = (temp > factor) ? temp : factor;
                    tolerance_surpassed = true;
                }
            }
            else
            {
                if((q(i) - this->tc_params_.limits_min[i]) < tolerance)    //Joint is nearer to the MINIMUM limit
                {
                    if(q_dot_ik(i) < 0)   //Joint moves towards the MIN limit
                    {
                        double temp = 1.0 / pow(0.5 + 0.5 * cos(M_PI * (q(i) - tolerance - this->tc_params_.limits_min[i]) / tolerance), 5.0);
                        factor = (temp > factor) ? temp : factor;
                        tolerance_surpassed = true;
                    }
                }
            }
        }
    }

    if (tolerance_surpassed)
    {
        ROS_ERROR_STREAM_THROTTLE(1, "Tolerance surpassed: Enforcing limits FOR ALL JOINT POSITIONS with factor = " << factor);
        for(int i = 0; i < q_dot_ik.rows() ; i++)
        {
            scaled_q_dot(i) = q_dot_ik(i) / factor;
        }
    }
    else
    {
        for(int i = 0; i < q_dot_ik.rows() ; i++)
        {
            scaled_q_dot(i) = q_dot_ik(i);
        }
    }

    return scaled_q_dot;
}
/* END LimiterAllJointPositions *********************************************************************************/

/* BEGIN LimiterAllJointVelocities ******************************************************************************/
/**
 * Enforce limits on all joint velocities to keep direction.
 * Limits all velocities according to the limits_vel vector if necessary.
 */
KDL::JntArray LimiterAllJointVelocities::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray q_dot_norm(q_dot_ik);
    double max_factor = 1.0;

    uint16_t maxDof = this->tc_params_.dof;
    std::vector<double> tmpLimits = this->tc_params_.limits_vel;
    if(this->tc_params_.kinematic_extension == BASE_ACTIVE)
    {
        maxDof += 6; // additional 6 DOF kinematic extension
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransX limit
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransY limit
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransZ limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotX limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotY limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotZ limit
    }

    for(uint16_t i=0; i < i < maxDof; ++i)
    {
        if(max_factor < std::fabs(q_dot_ik(i) / tmpLimits[i]))
        {
            max_factor = std::fabs(q_dot_ik(i) / tmpLimits[i]);
            //ROS_WARN("Value %d exceeds limit: Desired %f, Limit %f, Factor %f", i, q_dot_ik(i), tmpLimits[i], max_factor);
        }
    }

    if(max_factor > 1.0)
    {
        ROS_WARN_STREAM_THROTTLE(1, "Tolerance surpassed: Enforcing limits FOR ALL JOINT VELOCITIES with factor = " << max_factor);
        for(uint16_t i=0; i < i < maxDof; ++i)
        {
            q_dot_norm(i) = q_dot_ik(i) / max_factor;
        }
    }

    return q_dot_norm;
}
/* END LimiterAllJointVelocities ********************************************************************************/

/* BEGIN LimiterAllJointAccelerations ******************************************************************************/
/**
 * Enforce limits on all joint velocities based on acceleration limits to keep direction.
 * Limits all velocities according to the limits_acc vector if necessary.
 */
KDL::JntArray LimiterAllJointAccelerations::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray q_dot_norm(q_dot_ik);
    double max_factor = 1.0;

    //ToDo

    return q_dot_norm;
}
/* END LimiterAllJointAccelerations *****************************************************************************/

/* BEGIN LimiterIndividualJointPositions ************************************************************************/
/**
 * This implementation calculates limits for the joint positions without keeping the direction.
 * Then for each corresponding joint velocity an individual factor for scaling is calculated and then used.
 */
KDL::JntArray LimiterIndividualJointPositions::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray scaled_q_dot(q_dot_ik); // copy the whole q_dot array
    double tolerance = this->tc_params_.limits_tolerance / 180.0 * M_PI;
    double factor = 0.0;

    for(uint16_t i = 0; i < scaled_q_dot.rows() ; ++i)
    {
        if(i < chain_.getNrOfJoints())
        {
            if((this->tc_params_.limits_max[i] - q(i)) < tolerance)    //Joint is nearer to the MAXIMUM limit
            {
                if(scaled_q_dot(i) > 0.0)   //Joint moves towards the MAX limit
                {
                    factor = 1.0 / pow((0.5 + 0.5 * cos(M_PI * (q(i) + tolerance - this->tc_params_.limits_max[i]) / tolerance)), 5.0);
                    scaled_q_dot(i) = scaled_q_dot(i) / factor;
                }
            }
            else
            {
                if((q(i) - this->tc_params_.limits_min[i]) < tolerance)    //Joint is nearer to the MINIMUM limit
                {
                    if(scaled_q_dot(i) < 0.0)   //Joint moves towards the MIN limit
                    {
                        factor = 1.0 / pow(0.5 + 0.5 * cos(M_PI * (q(i) - tolerance - this->tc_params_.limits_min[i]) / tolerance), 5.0);
                        scaled_q_dot(i) = scaled_q_dot(i) / factor;
                    }
                }
            }
        }
    }

    return scaled_q_dot;
}
/* END LimiterIndividualJointPositions **************************************************************************/

/* BEGIN LimiterIndividualJointVelocities ***********************************************************************/
/**
 * This implementation calculates limits for the joint velocities without keeping the direction.
 * For each joint velocity in the vector an individual factor for scaling is calculated and used.
 */
KDL::JntArray LimiterIndividualJointVelocities::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray q_dot_norm(q_dot_ik);
    double max_factor = 1.0;

    uint16_t maxDof = this->tc_params_.dof;
    std::vector<double> tmpLimits = this->tc_params_.limits_vel;
    if(this->tc_params_.kinematic_extension == BASE_ACTIVE)
    {
        maxDof += 6; // additional 6 DOF kinematic extension
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransX limit
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransY limit
        tmpLimits.push_back(this->tc_params_.max_vel_lin_base); // BaseTransZ limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotX limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotY limit
        tmpLimits.push_back(this->tc_params_.max_vel_rot_base); // BaseRotZ limit
    }

    for(uint16_t i=0; i < maxDof; ++i)
    {
        max_factor = 1.0;
        if(max_factor < std::fabs(q_dot_ik(i) / tmpLimits[i]))
        {
            max_factor = std::fabs(q_dot_ik(i) / tmpLimits[i]);
            q_dot_norm(i) = q_dot_ik(i) / max_factor;
        }
    }

    return q_dot_norm;
}
/* END LimiterIndividualJointVelocities *************************************************************************/

/* BEGIN LimiterIndividualJointAccelerations ********************************************************************/
/**
 * This implementation scales velocities based on given limits for joint accelerations without keeping the direction.
 * For each joint velocity in the vector an individual factor for scaling is calculated and used.
 */
KDL::JntArray LimiterIndividualJointAccelerations::enforceLimits(const KDL::JntArray& q_dot_ik, const KDL::JntArray& q) const
{
    KDL::JntArray q_dot_norm(q_dot_ik);
    double max_factor = 1.0;

    //ToDo

    return q_dot_norm;
}
/* END LimiterIndividualJointAccelerations **********************************************************************/
