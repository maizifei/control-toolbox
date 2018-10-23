/**********************************************************************************************************************
This file is part of the Control Toolbox (https://adrlab.bitbucket.io/ct), copyright by ETH Zurich, Google Inc.
Licensed under Apache2 license (see LICENSE file in main directory)
**********************************************************************************************************************/

#pragma once

#include <ct/optcon/optcon.h>
#include "IKNLP.h"
#include "../InverseKinematicsBase.h"

namespace ct {
namespace rbd {

/*!
 * \tparam IKNLP the inverse kinematics NLP class (templated on correct kinematics)
 * \tparam VALIDATION_KIN the kinematics used to validate if the computed solution is correct
 *
 * \todo scale initialization noise according to joint limits
 */
template <typename IKNLP, typename VALIDATION_KIN>
class IKNLPSolverIpopt final :
    public ct::rbd::InverseKinematicsBase<IKNLP::Kinematics_t::NJOINTS, typename IKNLP::Scalar_t>,
	public ct::optcon::IpoptSolver
{
public:
    EIGEN_MAKE_ALIGNED_OPERATOR_NEW

	using KINEMATICS = typename IKNLP::Kinematics_t;
    using SCALAR = typename IKNLP::Scalar_t;

    using InverseKinematicsBase = ct::rbd::InverseKinematicsBase<KINEMATICS::NJOINTS, SCALAR>;
    using JointPosition_t = typename InverseKinematicsBase::JointPosition_t;
    using JointPositionsVector_t = typename InverseKinematicsBase::JointPositionsVector_t;
    using RigidBodyPoseTpl = typename InverseKinematicsBase::RigidBodyPoseTpl;

    IKNLPSolverIpopt() = delete;

    //! constructor
    IKNLPSolverIpopt(
    		std::shared_ptr<IKNLP> iknlp,
    		ct::optcon::NlpSolverSettings nlpSolverSettings,
			const size_t eeInd = 0):
    			ct::optcon::IpoptSolver(std::static_pointer_cast<ct::optcon::tpl::Nlp<SCALAR>>(iknlp), nlpSolverSettings),
    			iknlp_(iknlp),
				kinematics_(VALIDATION_KIN()),
				eeInd_(eeInd),
				noise_(0.0, 1.0) // zero mean unit variance
    {}

    //! constructor with additional inverse kinematics settings
    IKNLPSolverIpopt(
    		std::shared_ptr<IKNLP> iknlp,
    		ct::optcon::NlpSolverSettings nlpSolverSettings,
			const ct::rbd::InverseKinematicsSettings& ikSettings,
			const size_t eeInd = 0):
				IKNLPSolverIpopt(iknlp, nlpSolverSettings, eeInd)
    {
    	this->updateSettings(ikSettings);
    }

	void setInitialGuess(const JointPosition_t& q_init)
	{
		if (iknlp_)
			iknlp_->setInitialGuess(q_init);
		else
			throw std::runtime_error("IKNLPSolverIpopt needs to be be initialized before setting an initial guess!");
	}

    bool computeInverseKinematics(JointPositionsVector_t& ikSolutions,
        const RigidBodyPoseTpl& ee_W_base,
        const std::vector<size_t>& freeJoints = std::vector<size_t>()) override
    {
    	ikSolutions.clear();

    	iknlp_->getIKCostEvaluator()->setTargetPose(ee_W_base);

    	size_t count = 0;

    	bool solutionFound = false;

    	while(count < this->getSettings().maxNumTrials_ && !solutionFound)
    	{
			// set randomized initial guess if applicable
			if (this->getSettings().randomizeInitialGuess_ && count != 0)
			{
				setInitialGuess(noise_.gen<IKNLP::Kinematics_t::NJOINTS>());
			}

			// call underlying NLP solver
			solve();

			JointPosition_t sol = iknlp_->getSolution();

			// check if the solve was actually successful by forward evaluation and comparison
			RigidBodyPose forwardEval = kinematics_.getEEPoseInBase(eeInd_, sol);

			// evaluate if solution is near the desired pose and return
			if (forwardEval.isNear(ee_W_base, this->getSettings().validationTol_))
			{
				ikSolutions.push_back(sol);
				solutionFound = true;
			}

			count++;
		}

    	return solutionFound;
    }

    bool computeInverseKinematics(JointPositionsVector_t& ikSolutions,
        const RigidBodyPoseTpl& eeWorldPose,
        const RigidBodyPoseTpl& baseWorldPose,
        const std::vector<size_t>& freeJoints) override
    {
        return computeInverseKinematics(ikSolutions, eeWorldPose.inReferenceFrame(baseWorldPose), freeJoints);
    }

private:
    std::shared_ptr<IKNLP> iknlp_;

    VALIDATION_KIN kinematics_; // for validation (todo: need different way to include double-based kinematics
    size_t eeInd_;				// for validation
    ct::core::GaussianNoise noise_;
};

}  // rbd
}  // ct
