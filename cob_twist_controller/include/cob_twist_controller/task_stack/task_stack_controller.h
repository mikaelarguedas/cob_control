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
 * \date Date of creation: June, 2015
 *
 * \brief
 *   This header contains the implementation of the task stack controller
 *   and providing a task struct.
 *
 ****************************************************************/

#ifndef TASK_STACK_CONTROLLER_H_
#define TASK_STACK_CONTROLLER_H_

#include <Eigen/Dense>
#include <set>
#include <vector>
#include <stdint.h>

#include <ros/ros.h>

template
<typename PRIO>
struct Task
{

    // TODO: Use a activation flag here?
    PRIO prio_;
    Eigen::MatrixXd task_jacobian_;
    Eigen::VectorXd task_;
    std::string id_;
    bool is_active_;

    Task(PRIO prio, std::string id) : prio_(prio), id_(id), is_active_(true)
    {}

    Task(PRIO prio, std::string id, Eigen::MatrixXd task_jacobian, Eigen::VectorXd task)
    : prio_(prio), id_(id), task_jacobian_(task_jacobian), task_(task), is_active_(true)
    {}

    inline void setPriority(PRIO prio)
    {
        this->prio_ = prio;
    }

    inline bool operator<(const Task& other) const
    {
        return ( this->prio_ < other.prio_ );
    }

    inline bool operator>(const Task& other) const
    {
        return ( this->prio_ > other.prio_ );
    }

    inline bool operator==(const Task& other) const
    {
        return ( this->prio_ == other.prio_ );
    }
};

template
<typename PRIO>
class TaskStackController
{
    public:
        typedef typename std::vector<Task<PRIO> >::iterator TypedIter_t;

        ~TaskStackController()
        {
            this->tasks_.clear();
        }

        TaskStackController()
        {
            this->active_task_iter_ = this->tasks_.begin();
        }

        void clearAllTasks();

        void addTask(Task<PRIO> t);

        void deactivateTask(typename std::vector<Task<PRIO> >::iterator it);
        void deactivateTask(std::string task_id);
        void deactivateAllTasks();

        void activateAllTasks();
        void activateHighestPrioTask();

        typename std::vector<Task<PRIO> >::iterator getTasksBegin();

        typename std::vector<Task<PRIO> >::iterator getTasksEnd();

        typename std::vector<Task<PRIO> >::iterator nextActiveTask();

        typename std::vector<Task<PRIO> >::iterator beginTaskIter();

    private:
        // Use a vector instead of a set here. Set stores const references ->
        // so they cannot be changed.
        std::vector<Task<PRIO> > tasks_;

        TypedIter_t active_task_iter_;


};

/**
 * Insert new task sorted.
 */
template <typename PRIO>
void TaskStackController<PRIO>::addTask(Task<PRIO> t)
{
    TypedIter_t begin_it = this->tasks_.begin();
    TypedIter_t mem_it = this->tasks_.end();
    for(TypedIter_t it = this->tasks_.begin(); it != this->tasks_.end(); it++)
    {
        if(it->id_ == t.id_) // task already existent -> ignore add
        {
            mem_it = it;
            it->task_jacobian_ = t.task_jacobian_;
            it->task_ = t.task_;
            break;
        }
    }

    if (this->tasks_.end() == mem_it)
    {
        for(TypedIter_t it = this->tasks_.begin(); it != this->tasks_.end(); it++)
        {
            if(t.prio_ < it->prio_)
            {
                mem_it = it;
                break;
            }
        }

        if(this->tasks_.end() == mem_it)
        {
            this->tasks_.push_back(t);
        }
        else
        {
            this->tasks_.insert(mem_it, t);
        }
    }
}

template <typename PRIO>
void TaskStackController<PRIO>::activateAllTasks()
{
    for(TypedIter_t it = this->tasks_.begin(); it != this->tasks_.end(); it++)
    {
        it->is_active_ = true;
    }
}

template <typename PRIO>
void TaskStackController<PRIO>::activateHighestPrioTask()
{
    TypedIter_t it = this->tasks_.begin();
    if(this->tasks_.end() != it)
    {
        ROS_WARN_STREAM("Activation of highest prio task in stack: " << it->id_);
        it->is_active_ = true;
    }
}

template <typename PRIO>
typename std::vector<Task<PRIO> >::iterator TaskStackController<PRIO>::getTasksBegin()
{
    return this->tasks_.begin();
}


template <typename PRIO>
typename std::vector<Task<PRIO> >::iterator TaskStackController<PRIO>::getTasksEnd()
{
    return this->tasks_.end();
}


template <typename PRIO>
void TaskStackController<PRIO>::deactivateTask(typename std::vector<Task<PRIO> >::iterator it)
{
    if(std::find(this->tasks_.begin(), this->tasks_.end(), it) != this->tasks_.end())
    {
        it->is_active_ = false;
    }
}

template <typename PRIO>
void TaskStackController<PRIO>::deactivateTask(std::string task_id)
{
//    if(this->tasks_.size() <= 1)
//    {
//        return; // already all deactivated. One task must be left.
//    }

    for (TypedIter_t it = this->tasks_.begin(); it != this->tasks_.end(); it++)
    {
        if(it->id_ == task_id)
        {
            it->is_active_ = false;
            break;
        }
    }
}

template <typename PRIO>
void TaskStackController<PRIO>::deactivateAllTasks()
{
    for (TypedIter_t it = this->tasks_.begin(); it != this->tasks_.end(); it++)
    {
        it->is_active_ = false;
    }
}


template <typename PRIO>
typename std::vector<Task<PRIO> >::iterator TaskStackController<PRIO>::nextActiveTask()
{
    TypedIter_t ret = this->tasks_.end();

    while(this->tasks_.end() != this->active_task_iter_)
    {
        if(this->active_task_iter_->is_active_)
        {
            ret = this->active_task_iter_++;
            break;

        }

        this->active_task_iter_++;
    }

    return ret;
}


template <typename PRIO>
typename std::vector<Task<PRIO> >::iterator TaskStackController<PRIO>::beginTaskIter()
{
    this->active_task_iter_ = this->tasks_.begin();
    return this->active_task_iter_;
}

template <typename PRIO>
void TaskStackController<PRIO>::clearAllTasks()
{
    this->tasks_.clear();
    this->active_task_iter_ = this->tasks_.end();
}


typedef TaskStackController<uint32_t> TaskStackController_t;
typedef Task<uint32_t> Task_t;
typedef std::vector<Task_t >::iterator TaskSetIter_t;

#endif /* TASK_STACK_CONTROLLER_H_ */
