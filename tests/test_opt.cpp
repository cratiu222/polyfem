////////////////////////////////////////////////////////////////////////////////
#include <polyfem/State.hpp>

#include <polyfem/utils/StringUtils.hpp>
#include <polyfem/utils/Logger.hpp>
#include <polyfem/utils/JSONUtils.hpp>
#include <polyfem/solver/Optimizations.hpp>
#include <polyfem/solver/NonlinearSolver.hpp>

#include <polyfem/solver/forms/adjoint_forms/SumCompositeForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/CompositeForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/SpatialIntegralForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/AMIPSForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/WeightedVolumeForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/TransientForm.hpp>
#include <polyfem/solver/forms/adjoint_forms/BarrierForms.hpp>
#include <polyfem/solver/forms/adjoint_forms/SmoothingForms.hpp>

#include <polyfem/solver/forms/parametrization/Parametrizations.hpp>
#include <polyfem/solver/forms/parametrization/NodeCompositeParametrizations.hpp>
#include <polyfem/solver/forms/parametrization/SplineParametrizations.hpp>

#include <iostream>
#include <fstream>
#include <catch2/catch.hpp>
////////////////////////////////////////////////////////////////////////////////

using namespace polyfem;
using namespace solver;
using namespace polysolve;

namespace
{
	bool load_json(const std::string &json_file, json &out)
	{
		std::ifstream file(json_file);

		if (!file.is_open())
			return false;

		file >> out;

		out["root_path"] = json_file;

		return true;
	}

	std::string resolve_output_path(const std::string &output_dir, const std::string &path)
	{
		if (std::filesystem::path(path).is_absolute())
			return path;
		else
			return std::filesystem::weakly_canonical(std::filesystem::path(output_dir) / path).string();
	}

	bool save_mat(const Eigen::MatrixXd &mat, const std::string &file_name)
	{
		std::ofstream file(file_name);
		if (!file.is_open())
			return false;

		file << fmt::format("matrix size {} x {}\n", mat.rows(), mat.cols());
		file << mat;

		return true;
	}

	std::vector<double> read_energy(const std::string &file)
	{
		std::ifstream energy_out(file);
		std::vector<double> energies;
		std::string line;
		if (energy_out.is_open())
		{
			while (getline(energy_out, line))
			{
				energies.push_back(std::stod(line.substr(0, line.find(","))));
			}
		}
		double starting_energy = energies[0];
		double optimized_energy = energies[energies.size() - 1];

		for (int i = 0; i < energies.size(); ++i)
		{
			if (i == 0)
				std::cout << "initial " << energies[i] << std::endl;
			else if (i == energies.size() - 1)
				std::cout << "final " << energies[i] << std::endl;
			else
				std::cout << "step " << i << " " << energies[i] << std::endl;
		}

		return energies;
	}

} // namespace

#if defined(__linux__)

TEST_CASE("material-opt", "[optimization]")
{
	const std::string name = "material-opt";
	// run_opt_new(name);
	{
		const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + name + "/";
		json opt_args;
		if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
			log_and_throw_error("Failed to load optimization json file!");

		opt_args = apply_opt_json_spec(opt_args, false);

		for (auto &state_arg : opt_args["states"])
			state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

		json state_args = opt_args["states"];
		std::shared_ptr<solver::AdjointNLProblem> nl_problem;
		std::vector<std::shared_ptr<State>> states(state_args.size());
		Eigen::VectorXd x;
		{
			int i = 0;
			for (const json &args : state_args)
			{
				json cur_args;
				if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
					log_and_throw_error("Can't find json for State {}", i);

				states[i++] = create_state(cur_args);
			}

			const double E = 1e4;
			const double nu = 0.8;
			const double lambda = convert_to_lambda(states[0]->mesh->is_volume(), E, nu);
			const double mu = convert_to_mu(E, nu);
			x.resize(2);
			x << lambda, mu;
			x = x.array().log().eval();

			std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
			{
				const int n_elem = states[0]->mesh->n_elements();
				std::vector<std::shared_ptr<Parametrization>> map_list = {std::make_shared<ExponentialMap>(), std::make_shared<PerBody2PerElem>(*(states[0]->mesh))};
				CompositeParametrization composite_map(map_list);

				variable_to_simulations.push_back(std::make_shared<ElasticVariableToSimulation>(states[0], composite_map));
			}

			for (auto &v2s : variable_to_simulations)
				v2s->update(x);

			std::shared_ptr<SumCompositeForm> sum = std::dynamic_pointer_cast<SumCompositeForm>(create_form(opt_args["functionals"], variable_to_simulations, states));

			nl_problem = std::make_shared<AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);
		}

		auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
		CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));
	}
	auto energies = read_energy(name);

	REQUIRE(energies[0] == Approx(5.95421809553).epsilon(1e-3));
	REQUIRE(energies[energies.size() - 1] == Approx(0.00101793422213).epsilon(1e-3));
}

// TEST_CASE("friction-opt", "[optimization]")
// {
// 	run_opt_new("friction-opt");
// 	auto energies = read_energy("friction-opt");

// 	REQUIRE(energies[0] == Approx(0.000103767819516).epsilon(1e-1));
// 	REQUIRE(energies[energies.size() - 1] == Approx(3.26161994783e-07).epsilon(1e-1));
// }

// TEST_CASE("damping-opt", "[optimization]")
// {
// 	run_opt_new("damping-opt");
// 	auto energies = read_energy("damping-opt");

// 	REQUIRE(energies[0] == Approx(4.14517346014e-07).epsilon(1e-3));
// 	REQUIRE(energies[energies.size() - 1] == Approx(2.12684299792e-09).epsilon(1e-3));
// }

// // TEST_CASE("initial-opt", "[optimization]")
// // {
// // 	run_trajectory_opt("initial-opt");
// // 	auto energies = read_energy("initial-opt");

// // 	REQUIRE(energies[0] == Approx(0.147092).epsilon(1e-4));
// // 	REQUIRE(energies[energies.size() - 1] == Approx(0.109971).epsilon(1e-4));
// // }

TEST_CASE("topology-opt", "[optimization]")
{
	const std::string name = "topology-opt";
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + name + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	opt_args = apply_opt_json_spec(opt_args, false);

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::shared_ptr<solver::AdjointNLProblem> nl_problem;
	std::vector<std::shared_ptr<State>> states(state_args.size());
	Eigen::VectorXd x;
	std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
	{
		// create simulators based on json inputs
		int i = 0;
		for (const json &args : state_args)
		{
			json cur_args;
			if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
				log_and_throw_error("Can't find json for State {}", i);

			states[i++] = create_state(cur_args);
		}

		// define mappings from optimization variable x to material parameters in states
		for (const auto &arg : opt_args["variable_to_simulation"])
			variable_to_simulations.push_back(create_variable_to_simulation(arg, states));

		// initialize optimization variable and assign elastic parameters to simulators
		int ndof = 0;
		for (const auto &arg : opt_args["parameters"])
			ndof += arg["number"].get<int>();

		x.setZero(ndof);
		int accumulative = 0;
		for (const auto &arg : opt_args["parameters"])
		{
			Eigen::VectorXd tmp(arg["number"].get<int>());
			if (arg["initial"].is_array())
				nlohmann::adl_serializer<Eigen::VectorXd>::from_json(arg["initial"], tmp);
			else if (arg["initial"].is_number())
				tmp.setConstant(arg["initial"].get<double>());
			x.segment(accumulative, tmp.size()) = tmp;
			accumulative += tmp.size();
		}

		// define optimization objective -- sum of compliance of the same structure under different loads
		std::shared_ptr<SumCompositeForm> obj = std::dynamic_pointer_cast<SumCompositeForm>(create_form(opt_args["functionals"], variable_to_simulations, states));

		nl_problem = std::make_shared<solver::AdjointNLProblem>(obj, variable_to_simulations, states, opt_args);

		nl_problem->solution_changed(x);
	}

	auto nl_solver = std::make_shared<cppoptlib::MMASolver<solver::AdjointNLProblem>>(opt_args["solver"]["nonlinear"], 0.);

	// TODO: Define in json interface
	// nonlinear inequality constraints g(x) < 0
	{
		auto obj1 = std::make_shared<WeightedVolumeForm>(CompositeParametrization({std::make_shared<LinearFilter>(*(states[0]->mesh), 0.1)}), *(states[0]));
		obj1->set_weight(1 / 1.2);
		auto obj2 = std::make_shared<PlusConstCompositeForm>(obj1, -1);
		nl_solver->set_constraints({obj2});
	}

	// run the optimization for a few steps
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));

	// check if the objective at these steps are correct
	auto energies = read_energy(name);
	REQUIRE(energies[0] == Approx(136.013542195).epsilon(1e-4));
	REQUIRE(energies[energies.size() - 1] == Approx(0.726565337285).epsilon(1e-4));
}

TEST_CASE("AMIPS-debug", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "AMIPS-debug" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x(2);
	x << 0., 1.;

	std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
	{
		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToBoundaryNodesExclusive({}, *states[0], {1})));
	}

	auto obj1 = std::make_shared<AMIPSForm>(variable_to_simulations, *states[0], json());
	obj1->set_weight(1.0);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj1});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));
}

TEST_CASE("shape-stress-opt-debug", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "shape-stress-opt-new" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x;
	Eigen::MatrixXd V;
	Eigen::MatrixXi F;
	states[0]->get_vf(V, F);
	x = utils::flatten(V);

	std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
	{
		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], CompositeParametrization()));
	}

	auto obj1 = std::make_shared<StressNormForm>(variable_to_simulations, *states[0], opt_args["functionals"][0]);
	obj1->set_weight(1.0);

	auto obj2 = std::make_shared<AMIPSForm>(variable_to_simulations, *states[0], json());
	obj2->set_weight(1.0);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj2});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	nl_problem->solution_changed(x);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));

	auto energies = read_energy("shape-stress-opt-new");
}

TEST_CASE("shape-stress-opt-new", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "shape-stress-opt-new" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x;
	int opt_bnodes = 0;
	int opt_inodes = 0;
	int dim;
	{
		const auto &mesh = states[0]->mesh;
		const auto &bases = states[0]->bases;
		const auto &gbases = states[0]->geom_bases();
		dim = mesh->dimension();

		std::set<int> node_ids;
		std::set<int> total_bnode_ids;
		for (const auto &lb : states[0]->total_local_boundary)
		{
			const int e = lb.element_id();
			for (int i = 0; i < lb.size(); ++i)
			{
				const int primitive_global_id = lb.global_primitive_id(i);
				const int boundary_id = mesh->get_boundary_id(primitive_global_id);
				const auto nodes = gbases[e].local_nodes_for_primitive(primitive_global_id, *mesh);

				if (boundary_id == 10 || boundary_id == 11)
					for (long n = 0; n < nodes.size(); ++n)
						node_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
				for (long n = 0; n < nodes.size(); ++n)
					total_bnode_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
			}
		}
		std::vector<int> selected_bnode_ids;
		std::set_difference(total_bnode_ids.begin(), total_bnode_ids.end(), node_ids.begin(), node_ids.end(), std::back_inserter(selected_bnode_ids));
		opt_bnodes = selected_bnode_ids.size();

		node_ids = {};
		for (int e = 0; e < gbases.size(); e++)
		{
			const int body_id = mesh->get_body_id(e);
			if (body_id == 1)
				for (const auto &gbs : gbases[e].bases)
					for (const auto &g : gbs.global())
						if (!total_bnode_ids.count(g.index))
							node_ids.insert(g.index);
		}
		opt_inodes = node_ids.size();
	}
	x.resize((opt_bnodes + opt_inodes) * dim);

	std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
	{
		std::vector<std::shared_ptr<Parametrization>> boundary_map_list = {std::make_shared<SliceMap>(0, opt_bnodes * dim)};
		std::vector<std::shared_ptr<Parametrization>> interior_map_list = {std::make_shared<SliceMap>(opt_bnodes * dim, (opt_bnodes + opt_inodes) * dim)};

		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToBoundaryNodesExclusive(boundary_map_list, *states[0], {10, 11})));
		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToInteriorNodes(interior_map_list, *states[0], 1)));
	}

	{
		Eigen::MatrixXd V;
		Eigen::MatrixXi F;
		states[0]->get_vf(V, F);
		Eigen::VectorXd V_flat = utils::flatten(V);

		auto b_idx = variable_to_simulations[0]->get_parametrization().get_output_indexing(x);
		assert(b_idx.size() == (opt_bnodes * dim));
		for (int i = 0; i < opt_bnodes; ++i)
			for (int k = 0; k < dim; ++k)
				x(i * dim + k) = V_flat(b_idx(i * dim + k));

		auto i_idx = variable_to_simulations[1]->get_parametrization().get_output_indexing(x);
		assert(i_idx.size() == (opt_inodes * dim));
		for (int i = 0; i < opt_inodes; ++i)
			for (int k = 0; k < dim; ++k)
				x(opt_bnodes * dim + i * dim + k) = V_flat(i_idx(i * dim + k));
	}

	auto obj1 = std::make_shared<StressNormForm>(variable_to_simulations, *states[0], opt_args["functionals"][0]);
	obj1->set_weight(1.0);

	auto obj2 = std::make_shared<AMIPSForm>(variable_to_simulations, *states[0], json());
	obj2->set_weight(0.01);

	auto obj3 = std::make_shared<BoundarySmoothingForm>(variable_to_simulations, *states[0], false, 2);
	obj3->set_weight(8);

	std::vector<std::shared_ptr<AdjointForm>> volume_form = {std::make_shared<VolumeForm>(variable_to_simulations, *states[0], opt_args["functionals"][0])};
	Eigen::VectorXd volume_bounds(2);
	volume_bounds << 0, 2.36226e-1;
	auto obj4 = std::make_shared<InequalityConstraintForm>(volume_form, volume_bounds);
	obj4->set_weight(10);

	auto obj5 = std::make_shared<CollisionBarrierForm>(variable_to_simulations, *states[0], 1e-3);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj1, obj2, obj3, obj4, obj5});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	nl_problem->solution_changed(x);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));

	auto energies = read_energy("shape-stress-opt-new");

	REQUIRE(energies[0] == Approx(8.9795).epsilon(1e-4));
	REQUIRE(energies[energies.size() - 1] == Approx(8.75743).epsilon(1e-4));

	// REQUIRE(energies[0] == Approx(12.0735).epsilon(1e-4));
	// REQUIRE(energies[energies.size() - 1] == Approx(11.3886).epsilon(1e-4));
}

// TEST_CASE("shape-trajectory-surface-opt-new", "[optimization]")
// {
// 	run_opt_new("shape-trajectory-surface-opt-new");
// 	auto energies = read_energy("shape-trajectory-surface-opt-new");

// 	REQUIRE(energies[0] == Approx(6.1658e-05).epsilon(1e-3));
// 	REQUIRE(energies[energies.size() - 1] == Approx(3.6194e-05).epsilon(1e-3));
// }

TEST_CASE("shape-trajectory-surface-opt", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "shape-trajectory-surface-opt-bspline" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x;
	int opt_bnodes = 0;
	int dim;
	{
		const auto &mesh = states[0]->mesh;
		const auto &bases = states[0]->bases;
		const auto &gbases = states[0]->geom_bases();
		dim = mesh->dimension();

		std::set<int> node_ids;
		std::set<int> total_bnode_ids;
		for (const auto &lb : states[0]->total_local_boundary)
		{
			const int e = lb.element_id();
			for (int i = 0; i < lb.size(); ++i)
			{
				const int primitive_global_id = lb.global_primitive_id(i);
				const int boundary_id = mesh->get_boundary_id(primitive_global_id);
				const auto nodes = gbases[e].local_nodes_for_primitive(primitive_global_id, *mesh);

				if (boundary_id == 4)
					for (long n = 0; n < nodes.size(); ++n)
						node_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
			}
		}
		opt_bnodes = node_ids.size();
	}
	x.resize(opt_bnodes * dim);

	std::vector<std::shared_ptr<VariableToSimulation>>
		variable_to_simulations;
	{
		std::vector<std::shared_ptr<Parametrization>> spline_boundary_map_list = {};

		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToBoundaryNodes(spline_boundary_map_list, *states[0], {4})));
	}

	{
		Eigen::MatrixXd V;
		Eigen::MatrixXi F;
		states[0]->get_vf(V, F);
		Eigen::VectorXd V_flat = utils::flatten(V);

		auto b_idx = variable_to_simulations[0]->get_parametrization().get_output_indexing(x);
		assert(b_idx.size() == (opt_bnodes * dim));
		for (int i = 0; i < opt_bnodes; ++i)
			for (int k = 0; k < dim; ++k)
				x(i * dim + k) = V_flat(b_idx(i * dim + k));
	}

	auto target = std::make_shared<TargetForm>(variable_to_simulations, *states[0], opt_args["functionals"][0]);
	target->set_reference(states[1], {2});
	auto obj1 = std::make_shared<TransientForm>(variable_to_simulations, 4, 0.1, "final", target);
	obj1->set_weight(1.0);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj1});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	nl_problem->solution_changed(x);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	// Expect this simple example to converge
	nl_solver->minimize(*nl_problem, x);

	auto energies = read_energy("shape-trajectory-surface-opt-bspline");

	REQUIRE(energies[0] == Approx(8.1934e-04).epsilon(1e-3));
	REQUIRE(energies[energies.size() - 1] == Approx(6.809e-14).epsilon(1e-3));
}

TEST_CASE("shape-trajectory-surface-opt-bspline", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "shape-trajectory-surface-opt-bspline" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x;
	int opt_bnodes = 0;
	int dim;
	{
		const auto &mesh = states[0]->mesh;
		const auto &bases = states[0]->bases;
		const auto &gbases = states[0]->geom_bases();
		dim = mesh->dimension();

		std::set<int> node_ids;
		std::set<int> total_bnode_ids;
		for (const auto &lb : states[0]->total_local_boundary)
		{
			const int e = lb.element_id();
			for (int i = 0; i < lb.size(); ++i)
			{
				const int primitive_global_id = lb.global_primitive_id(i);
				const int boundary_id = mesh->get_boundary_id(primitive_global_id);
				const auto nodes = gbases[e].local_nodes_for_primitive(primitive_global_id, *mesh);

				if (boundary_id == 4)
					for (long n = 0; n < nodes.size(); ++n)
						node_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
			}
		}
		opt_bnodes = node_ids.size();
	}
	x.resize(4);

	Eigen::MatrixXd initial_control_points(4, 2);
	initial_control_points << 0, -1,
		0.66666667, -0.33333333,
		0.66666667, 0.33333333,
		0, 1;
	initial_control_points.col(0).array() += 0.5;

	Eigen::MatrixXd final_control_points(4, 2);
	final_control_points << 0, -1,
		0.7, -0.5,
		0.7, 0.5,
		0, 1;
	final_control_points.col(0).array() += 0.5;

	Eigen::VectorXd knots(8);
	knots << 0,
		0,
		0,
		0,
		1,
		1,
		1,
		1;

	std::vector<std::shared_ptr<VariableToSimulation>>
		variable_to_simulations;
	{
		std::vector<std::shared_ptr<Parametrization>> spline_boundary_map_list = {std::make_shared<BSplineParametrization1DTo2D>(initial_control_points, knots, opt_bnodes, true)};

		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToBoundaryNodes(spline_boundary_map_list, *states[0], 4)));
	}

	{
		Eigen::MatrixXd V;
		Eigen::MatrixXi F;
		states[0]->get_vf(V, F);
		Eigen::VectorXd V_flat = utils::flatten(V);

		auto b_idx = variable_to_simulations[0]->get_parametrization().get_output_indexing(x);
		assert(b_idx.size() == (opt_bnodes * dim));
		Eigen::VectorXd y(opt_bnodes * dim);
		for (int i = 0; i < opt_bnodes; ++i)
			for (int k = 0; k < dim; ++k)
				y(i * dim + k) = V_flat(b_idx(i * dim + k));

		x = variable_to_simulations[0]->get_parametrization().inverse_eval(y);

		assert((x - utils::flatten(initial_control_points).segment(2, 4)).norm() < 1e-12);
	}

	auto target = std::make_shared<TargetForm>(variable_to_simulations, *states[0], opt_args["functionals"][0]);
	target->set_reference(states[1], {2});
	auto obj1 = std::make_shared<TransientForm>(variable_to_simulations, 4, 0.1, "final", target);
	obj1->set_weight(1.0);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj1});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	nl_problem->solution_changed(x);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));

	auto energies = read_energy("shape-trajectory-surface-opt-bspline");

	REQUIRE(energies[0] == Approx(6.1658e-05).epsilon(1e-3));
	REQUIRE(energies[energies.size() - 1] == Approx(1.056e-8).epsilon(1e-3));
}

TEST_CASE("shape-stress-bbw-opt", "[optimization]")
{
	const std::string root_folder = POLYFEM_DATA_DIR + std::string("/../optimizations/") + "shape-stress-bbw-opt" + "/";
	json opt_args;
	if (!load_json(resolve_output_path(root_folder, "run.json"), opt_args))
		log_and_throw_error("Failed to load optimization json file!");

	for (auto &state_arg : opt_args["states"])
		state_arg["path"] = resolve_output_path(root_folder, state_arg["path"]);

	json state_args = opt_args["states"];
	std::vector<std::shared_ptr<State>> states(state_args.size());
	int i = 0;
	for (const json &args : state_args)
	{
		json cur_args;
		if (!load_json(utils::resolve_path(args["path"], root_folder, false), cur_args))
			log_and_throw_error("Can't find json for State {}", i);

		states[i++] = create_state(cur_args, spdlog::level::level_enum::err);
	}

	Eigen::VectorXd x;
	int opt_bnodes = 0;
	int opt_boundary_var = 5;
	int opt_inodes = 0;
	int dim;
	{
		const auto &mesh = states[0]->mesh;
		const auto &bases = states[0]->bases;
		const auto &gbases = states[0]->geom_bases();
		dim = mesh->dimension();

		std::set<int> total_bnode_ids;
		std::set<int> node_ids;
		for (const auto &lb : states[0]->total_local_boundary)
		{
			const int e = lb.element_id();
			for (int i = 0; i < lb.size(); ++i)
			{
				const int primitive_global_id = lb.global_primitive_id(i);
				const int boundary_id = mesh->get_boundary_id(primitive_global_id);
				const auto nodes = gbases[e].local_nodes_for_primitive(primitive_global_id, *mesh);

				if (boundary_id == 2)
					for (long n = 0; n < nodes.size(); ++n)
						node_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
				for (long n = 0; n < nodes.size(); ++n)
					total_bnode_ids.insert(gbases[e].bases[nodes(n)].global()[0].index);
			}
		}
		opt_bnodes = node_ids.size();

		node_ids = {};
		for (int e = 0; e < gbases.size(); e++)
		{
			const int body_id = mesh->get_body_id(e);
			if (body_id == 1)
				for (const auto &gbs : gbases[e].bases)
					for (const auto &g : gbs.global())
						if (!total_bnode_ids.count(g.index))
							node_ids.insert(g.index);
		}
		opt_inodes = node_ids.size();
	}
	x.resize((opt_boundary_var + opt_inodes) * dim);
	Eigen::MatrixXd V, V_surface;
	Eigen::MatrixXi F, F_surface;
	states[0]->get_vf(V, F);
	Eigen::VectorXd V_flat = utils::flatten(V);

	std::vector<std::shared_ptr<VariableToSimulation>> variable_to_simulations;
	{
		std::vector<std::shared_ptr<Parametrization>> boundary_map_list = {std::make_shared<SliceMap>(0, opt_boundary_var * dim), std::make_shared<BoundedBiharmonicWeights2Dto3D>(opt_boundary_var, opt_bnodes, *states[0], 2)};
		std::vector<std::shared_ptr<Parametrization>> interior_map_list = {std::make_shared<SliceMap>(opt_boundary_var * dim, (opt_boundary_var + opt_inodes) * dim)};

		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToBoundaryNodes(boundary_map_list, *states[0], 2)));
		variable_to_simulations.push_back(std::make_shared<ShapeVariableToSimulation>(states[0], VariableToInteriorNodes(interior_map_list, *states[0], 1)));
	}

	{
		auto b_idx = variable_to_simulations[0]->get_parametrization().get_output_indexing(x);
		assert(b_idx.size() == (opt_bnodes * dim));
		Eigen::VectorXd y(opt_bnodes * dim);
		for (int i = 0; i < opt_bnodes; ++i)
			for (int k = 0; k < dim; ++k)
				y(i * dim + k) = V_flat(b_idx(i * dim + k));

		x.segment(0, opt_boundary_var * dim) = variable_to_simulations[0]->get_parametrization().inverse_eval(y);
	}

	{
		auto b_idx = variable_to_simulations[1]->get_parametrization().get_output_indexing(x);
		assert(b_idx.size() == (opt_inodes * dim));
		Eigen::VectorXd y(opt_inodes * dim);
		for (int i = 0; i < opt_inodes; ++i)
			for (int k = 0; k < dim; ++k)
				y(i * dim + k) = V_flat(b_idx(i * dim + k));

		x.segment(opt_boundary_var * dim, opt_inodes * dim) = variable_to_simulations[1]->get_parametrization().inverse_eval(y);
	}

	auto obj1 = std::make_shared<StressNormForm>(variable_to_simulations, *states[0], opt_args["functionals"][0]);
	obj1->set_weight(1.0e-12);

	auto obj2 = std::make_shared<AMIPSForm>(variable_to_simulations, *states[0], json());
	obj2->set_weight(1.0);

	std::vector<std::shared_ptr<AdjointForm>> forms({obj1, obj2});

	auto sum = std::make_shared<SumCompositeForm>(variable_to_simulations, forms);
	sum->set_weight(1.0);

	std::shared_ptr<solver::AdjointNLProblem> nl_problem = std::make_shared<solver::AdjointNLProblem>(sum, variable_to_simulations, states, opt_args);

	nl_problem->solution_changed(x);

	auto nl_solver = make_nl_solver<AdjointNLProblem>(opt_args["solver"]["nonlinear"]);
	CHECK_THROWS_WITH(nl_solver->minimize(*nl_problem, x), Catch::Matchers::Contains("Reached iteration limit"));

	auto energies = read_energy("shape-stress-bbw-opt");

	REQUIRE(energies[0] == Approx(26.158).epsilon(1e-3));
	REQUIRE(energies[energies.size() - 1] == Approx(24.846).epsilon(1e-3));
}

// TEST_CASE("multiparameter-sdf-trajectory-surface-opt", "[optimization]")
// {
// 	run_opt_new("multiparameter-sdf-trajectory-surface-opt");
// 	auto energies = read_energy("multiparameter-sdf-trajectory-surface-opt");

// 	REQUIRE(energies[0] == Approx(0.15327).epsilon(1e-3));
// 	REQUIRE(energies[energies.size() - 1] == Approx(0.11259).epsilon(1e-3));
// }

// // TEST_CASE("sdf-test", "[optimization]")
// // {
// // 	const std::string path = POLYFEM_DATA_DIR + std::string("/../optimizations/multiparameter-sdf-trajectory-surface-opt");

// // 	json in_args;
// // 	load_json(path + "/state.json", in_args);
// // 	auto state = create_state(in_args);
// // 	json args = R"(
// // 		{
// // 			"surface_selection": [3, 4]
// // 		}
// // 	)"_json;
// // 	SDFTargetObjective sdf(*state, nullptr, args);
// // 	Eigen::MatrixXd control_points(4, 2);
// // 	control_points << 0, 1,
// // 		0.5, 0.7,
// // 		0.5, 0.3,
// // 		0, 0;
// // 	Eigen::VectorXd knots(8);
// // 	knots << 0, 0, 0, 0, 1, 1, 1, 1;
// // 	Eigen::MatrixXd delta(2, 1);
// // 	delta << 0.1, 0.1;
// // 	sdf.set_bspline_target(control_points, knots, delta(0));

// // 	int sampling = (int)(3 / delta(0));
// // 	int upsampling = 1000;
// // 	Eigen::MatrixXd distance(sampling, sampling);
// // 	Eigen::MatrixXd grad_x;
// // 	Eigen::MatrixXd grad_y;
// // 	grad_x.setZero(sampling, sampling);
// // 	grad_y.setZero(sampling, sampling);
// // 	Eigen::MatrixXd bounds(2, 2);
// // 	bounds << -1, 2,
// // 		-1, 2;
// // 	for (int i = 0; i < sampling; ++i)
// // 		for (int j = 0; j < sampling; ++j)
// // 		{
// // 			double x = bounds(0, 0) + j * (bounds(0, 1) - bounds(0, 0)) / (double)sampling;
// // 			double y = bounds(1, 1) - i * (bounds(1, 1) - bounds(1, 0)) / (double)sampling;
// // 			Eigen::MatrixXd point(2, 1);
// // 			point << x, y;
// // 			double d;
// // 			Eigen::MatrixXd g;
// // 			sdf.compute_distance(point, d);
// // 			distance(i, j) = d;
// // 			grad_x(i, j) = 0;
// // 			grad_y(i, j) = 0;
// // 		}
// // 	for (int i = 1; i < sampling - 1; ++i)
// // 		for (int j = 1; j < sampling - 1; ++j)
// // 		{
// // 			grad_x(i, j) = (1. / 2. / delta(0)) * (distance(i, j + 1) - distance(i, j - 1));
// // 			grad_y(i, j) = (1. / 2. / delta(0)) * (distance(i - 1, j) - distance(i + 1, j));
// // 		}

// // 	save_mat(distance, "orig_distance.txt");
// // 	save_mat(grad_x, "orig_grad_x.txt");
// // 	save_mat(grad_y, "orig_grad_y.txt");

// // 	distance.resize(upsampling, upsampling);
// // 	grad_x.resize(upsampling, upsampling);
// // 	grad_y.resize(upsampling, upsampling);
// // 	for (int i = 0; i < upsampling; ++i)
// // 		for (int j = 0; j < upsampling; ++j)
// // 		{
// // 			double x = bounds(0, 0) + j * (bounds(0, 1) - bounds(0, 0)) / (double)upsampling;
// // 			double y = bounds(1, 1) - i * (bounds(1, 1) - bounds(1, 0)) / (double)upsampling;
// // 			Eigen::MatrixXd point(2, 1);
// // 			point << x, y;
// // 			double d;
// // 			Eigen::MatrixXd g;
// // 			sdf.evaluate(point, d, g);
// // 			// sdf.compute_distance(point, d);
// // 			distance(i, j) = d;
// // 			grad_x(i, j) = g(0);
// // 			grad_y(i, j) = g(1);
// // 		}

// // 	save_mat(distance, "distance.txt");
// // 	save_mat(grad_x, "grad_x.txt");
// // 	save_mat(grad_y, "grad_y.txt");
// // }

// TEST_CASE("3d-bspline-shape-trajectory-opt", "[optimization]")
// {
// 	run_opt_new("3d-bspline-shape-trajectory-opt");
// 	auto energies = read_energy("3d-bspline-shape-trajectory-opt");

// 	REQUIRE(energies[0] == Approx(0.00473695).epsilon(1e-3));
// 	REQUIRE(energies[energies.size() - 1] == Approx(0.0004626948).epsilon(1e-4));
// }

// TEST_CASE("3d-bspline-shape-matching", "[optimization]")
// {
// 	run_opt_new("3d-bspline-shape-matching");
// 	auto energies = read_energy("3d-bspline-shape-matching");

// 	REQUIRE(energies[0] == Approx(1.86898e-05).epsilon(1e-4));
// 	REQUIRE(energies[energies.size() - 1] == Approx(1.85359e-05).epsilon(1e-4));
// }

#endif