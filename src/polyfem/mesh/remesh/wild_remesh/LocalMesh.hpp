#pragma once

#include <polyfem/utils/Types.hpp>

#include <Eigen/Core>

namespace polyfem::mesh
{
	template <typename M>
	class LocalMesh
	{
	protected:
		using Tuple = typename M::Tuple;

	public:
		LocalMesh(
			const M &m,
			const std::vector<Tuple> &triangle_tuples,
			const bool include_global_boundary);

		/// @brief Construct a local mesh as an n-ring around a vertex.
		static LocalMesh<M> n_ring(
			const M &m,
			const Tuple &center,
			const int n,
			const bool include_global_boundary);

		/// @brief Construct a local mesh as an n-ring around a vertex.
		static LocalMesh<M> flood_fill_n_ring(
			const M &m,
			const Tuple &center,
			const double area,
			const bool include_global_boundary);

		static LocalMesh<M> ball_selection(
			const M &m,
			VectorNd center,
			const double rel_radius,
			const bool include_global_boundary);

		int num_triangles() const { return m_triangles.rows(); }
		int num_local_vertices() const { return m_num_local_vertices; }
		int num_vertices() const { return m_rest_positions.rows(); }

		const Eigen::MatrixXd &rest_positions() const { return m_rest_positions; }
		const Eigen::MatrixXd &positions() const { return m_positions; }
		Eigen::MatrixXd displacements() const { return m_positions - m_rest_positions; }

		const Eigen::MatrixXd &prev_displacements() const { return m_prev_displacements; }
		const Eigen::MatrixXd &prev_velocities() const { return m_prev_velocities; }
		const Eigen::MatrixXd &prev_accelerations() const { return m_prev_accelerations; }

		const Eigen::MatrixXd &friction_gradient() const { return m_friction_gradient; }

		const Eigen::MatrixXi &boundary_edges() const { return m_boundary_edges; }
		const Eigen::MatrixXi &triangles() const { return m_triangles; }

		const std::unordered_map<int, int> &global_to_local() const { return m_global_to_local; }
		const std::vector<int> &local_to_global() const { return m_local_to_global; }

		const std::vector<int> &fixed_vertices() const { return m_fixed_vertices; }
		const std::vector<int> &boundary_ids() const { return m_boundary_ids; }
		const std::vector<int> &body_ids() const { return m_body_ids; }

		void reorder_vertices(const Eigen::VectorXi &permutation);

	protected:
		void remove_duplicate_fixed_vertices();
		void init_local_to_global();
		void init_vertex_attributes(const M &m);

		Eigen::MatrixXd m_rest_positions;
		Eigen::MatrixXd m_positions;

		// TODO: replace this with a time integrator object
		Eigen::MatrixXd m_prev_displacements;
		Eigen::MatrixXd m_prev_velocities;
		Eigen::MatrixXd m_prev_accelerations;

		Eigen::MatrixXd m_friction_gradient;

		Eigen::MatrixXi m_boundary_edges;
		Eigen::MatrixXi m_triangles;

		int m_num_local_vertices;
		std::unordered_map<int, int> m_global_to_local;
		std::vector<int> m_local_to_global;

		std::vector<int> m_fixed_vertices;
		std::vector<int> m_boundary_ids; // only for boundary edges
		std::vector<int> m_body_ids;
	};
} // namespace polyfem::mesh