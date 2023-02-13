#pragma once

#include <polyfem/State.hpp>

namespace polyfem::solver
{
    enum class ParameterType {
        Shape, Material, FrictionCoeff, DampingCoeff, InitialCondition, DirichletBC, MacroStrain
    };

	enum class SpatialIntegralType
	{
		VOLUME,
		SURFACE,
		VERTEX_SUM
	};

	class AdjointTools
	{
	public:
		AdjointTools()
		{
		}
		
		static void compute_adjoint_term(
			const State &state,
			const Eigen::MatrixXd &adjoints, 
			const ParameterType &param_name,
			Eigen::VectorXd &term);

		static double integrate_objective(
			const State &state,
			const IntegrableFunctional &j,
			const Eigen::MatrixXd &solution,
			const std::set<int> &interested_ids, // either body id or surface id
			const SpatialIntegralType spatial_integral_type,
			const int cur_step = 0);
		static void dJ_du_step(
			const State &state,
			const IntegrableFunctional &j,
			const Eigen::MatrixXd &solution,
			const std::set<int> &interested_ids,
			const SpatialIntegralType spatial_integral_type,
			const int cur_step,
			Eigen::VectorXd &term);
		static void dJ_macro_strain_adjoint_term(
			const State &state,
			const Eigen::MatrixXd &sol,
			const Eigen::MatrixXd &adjoint,
			Eigen::VectorXd &one_form);
		static void compute_macro_strain_derivative_functional_term(
			const State &state,
			const Eigen::MatrixXd &solution,
			const IntegrableFunctional &j,
			const std::set<int> &interested_ids, // either body id or surface id
			const SpatialIntegralType spatial_integral_type,
			Eigen::VectorXd &term,
			const int cur_time_step);
		static void compute_shape_derivative_functional_term(
			const State &state,
			const Eigen::MatrixXd &solution,
			const IntegrableFunctional &j,
			const std::set<int> &interested_ids, // either body id or surface id
			const SpatialIntegralType spatial_integral_type,
			Eigen::VectorXd &term,
			const int cur_time_step);
		static void dJ_shape_static_adjoint_term(
			const State &state,
			const Eigen::MatrixXd &sol,
			const Eigen::MatrixXd &adjoint,
			Eigen::VectorXd &one_form);
		static void dJ_shape_transient_adjoint_term(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
		static void dJ_material_static(
			const State &state,
			const Eigen::MatrixXd &sol,
			const Eigen::MatrixXd &adjoint,
			Eigen::VectorXd &one_form);
		static void dJ_material_transient(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
		static void dJ_friction_transient(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
		static void dJ_damping_transient(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
		static void dJ_initial_condition(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
		static void dJ_dirichlet_transient(
			const State &state,
			const Eigen::MatrixXd &adjoint_nu,
			const Eigen::MatrixXd &adjoint_p,
			Eigen::VectorXd &one_form);
	};
} // namespace polyfem::solver
