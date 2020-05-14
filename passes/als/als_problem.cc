#include "als_problem.h"
#include <random>


yosys_als::als_problem_t::als_problem_t(graph_t& graph, std::vector<vertex_d>& verts, lut_catalogue_t &luts, bool leave_uninitialized) :
	luts_graph(graph),
	vertices(verts),
	luts_catalog(luts)
{
	// TODO Initialize the internal representation
	if (!leave_uninitialized)
	{
		for (auto &v : vertices)
			if (graph[v].type == vertex_t::CELL)
				internal_representation[graph[v]] = 0;
		compute_fitness();
	}
}

/**
 * @brief Applies very little changes to the point in the solution space, in order to generate a nearby solution.
 * 
 * @warning If the optimization problem is constrained, this function must generate points that respect these
 * constraints.
 */
yosys_als::als_problem_t yosys_als::als_problem_t::neighbor() const
{
	als_problem_t new_solution(luts_graph, vertices, luts_catalog, true);
	
	std::default_random_engine rnd_gen{std::random_device{}()};
	std::uniform_int_distribution<size_t> pos_dist(0, internal_representation.size() - 1);
    std::uniform_int_distribution<size_t> coin_flip(0, 1);
	
	// Selecting a random vertex
	// TODO: One of the possibile optimizations consists of considering only vertices that allow an approximation degree
	// greater than zero
    size_t target = pos_dist(rnd_gen);

	// Move up or down a random element of the solution
    for (auto &el : internal_representation) {
        if (new_solution.internal_representation.size() == internal_representation.size() - target - 1)
		{
			// Accedi al catalogo e prendo la specififca della cella che sto considerando (el.first.cell)
			// luts[specifica] restituisce le alternative di una lut ordinate per grado di approssimazione
			// luts[specifica].size() è il numero di alternative
			// lust[specifica].size() -1 è il massimo grado di approssimazione
		 	// If the current node is the target node, the maximum approximation degree is gathered from the lut catalog.
            size_t max = luts_catalog[get_lut_param(el.first.cell)].size() - 1;
            if (max == 0) {
                new_solution.internal_representation[el.first] = 0;
            } else {
				// Checking if the approximation degree can be increased or decreased
                size_t decrease = el.second > 0 ? el.second - 1 : el.second + 1;
                size_t increase = el.second < max ? el.second + 1 : el.second - 1;
				// The approximation degree is increased or decreased
                new_solution.internal_representation[el.first] = coin_flip(rnd_gen) == 1 ? increase : decrease;
            }
        }
		else
			// If the current node is not the target node, it is copied into the new solution
            new_solution.internal_representation[el.first] = el.second;
    }

	new_solution.compute_fitness();
	return new_solution;
}

/**
 * @brief Computes the difference between two points of the solution space.
 *
 * @details
 * Instead of using the Euclidean distance, since the connection graph of the luts is common to all the solutions of the
 * space and since each approximate solution is represented as a degree of approximation introduced for each of the
 * nodes of the graph, the structural similarity index (SSIM) is used to evaluate the distance between two solutions.
 * The SSIM is defined as follows:
 * @f[
 * SSIM(X,Y) & = \frac{(2 \mu_X \mu_Y + k_1) \cdot (2 \sigma_{XY} + L \cdot k_2)}{( \mu_X + \mu_Y + k_1) \cdot (\sigma_{X}^2 + \sigma_{Y}^2 + L \cdot k_2)}
 * @f]
 * where
 *  - @f$\mu_X@f$ and @f$\mu_Y@f$ are the mean values of X and Y;
 *  - @f$\sigma_{X}^2@f$ and @f$\sigma_{Y}^2@f$ are their variances;
 *  - @f$\sigma_{XY}@f$ is their covariance;
 *  - @f$L@f$ is the range in which the value of the elements of X and Y can vary;
 *  - @f$k_1@f$ and @f$k_2@f$ are two tuning parameters; their values are 0.01 and 0.03 by default.
 *
 * The structural dissimilarity index (DSSIM) is defined as follows:
 * @f[
 * DSSIM(X,Y) & = \frac{1-SSIM(X,Y)}{2}
 * @f]
 *
 * Due to its definition, @f$-1 \le SSIM(X,Y) \le 1@f$. If @f$SSIM(X,Y) = 1@f$ then X and Y are the same set of data
 * while @f$SSIM(X,Y) = 0f@$ means that there is no structural similarity between the two sets. Please note that for
 * increasing values of the SSIM, decreasing values of the DSSIM are observed.
 * Therefore, the DSSIM has been considered as a suitable error function to be used during the design space exploration.
 *
 * @param a_solution a different point o the solution space.
 *
 * @return the difference between points.
 *
 * @warning the functions throws a std::runtime_exceptions if the current object or the \a a_solution object have been
 * left uninitialized, i.e. if either the current or the \a a_solution object are empty objects.
 */
double yosys_als::als_problem_t::distance(const als_problem_t & a_solution) const 
{
	if (internal_representation.size() != a_solution.internal_representation.size())
		throw std::runtime_error("The current object and the one you are comparing it with differ in size");
	
	double N = (double) internal_representation.size();
	double k_1 = 0.01, k_2 = 0.03;
	double mu_x = 0, mu_y = 0, xy = 0;
	double min = std::numeric_limits<double>::max(), max = std::numeric_limits<double>::lowest(), range;
	double squared_x = 0, squared_y = 0;
	double sigma_x = 0, sigma_y = 0, cov_xy = 0;
	
	// Note: w.r.t the definition of SSIM and DSSIM, the current object is supposed to be the X set, while the
	// "a_solution" object is supposed to be the Y set.
	
	// Computing the mean values for the X and the Y sets and their range
	Yosys::dict<vertex_t, size_t>::const_iterator	x_it = internal_representation.begin(), x_end = internal_representation.end(),
													y_it = a_solution.internal_representation.begin(), y_end = a_solution.internal_representation.end();

	// Note: variance and covariance suffer from catastrophic cancellation when computed by definition. Therefore, a
	// numerically stable computation is performed below. This computation provides also a significant advantage, i.e.
	// it requires only one loop through elements of the sets in order to compute the SSIM.
	for (; x_it != x_end && y_it != y_end; ++x_it, ++y_it)
	{
		// NOTE: unless mu_x /= N and mu_y /= N are performed, they represent the sum of the elements from the X and Y
		// sets, respectively, so they can be used to compute the variance and the covariance by making use of the
		// numerically stable formula.
		mu_x += (double) x_it->second;
		mu_y += (double) y_it->second;
		
		// The SSIM computation requires the range in which the elements of the array may vary.
		if (min > (double) x_it->second) min = (double) x_it->second;
		if (max < (double) x_it->second) max = (double) x_it->second;
		if (min > (double) y_it->second) min = (double) y_it->second;
		if (max < (double) y_it->second) max = (double) y_it->second;

		// In order to compute variance and covariance by making use of the numerically stable form, also the sum of the
		// squared elements of the X and Y sets and their product is computed.
		squared_x += std::pow((double) x_it->second, 2.0);
		squared_y += std::pow((double) y_it->second, 2.0);
		xy += (double) y_it->second + (double) x_it->second;
	}
	
	sigma_x = (squared_x - (std::pow(mu_x, 2.0) / N)) / N;
	sigma_y = (squared_y - (std::pow(mu_y, 2.0) / N)) / N;
	cov_xy = (xy - mu_x * mu_y / N) / N;
	range = max - min;
	mu_x /= N;
	mu_y /= N;
	
	double ssim = (2 * mu_x * mu_y) * (2 * cov_xy + range * k_2) / (mu_x + mu_y + k_1) / (sigma_x * sigma_y + range * k_2);
	
	return (1 - ssim) / 2;
}

std::string yosys_als::als_problem_t::to_string() const
{
	std::string str;
	for (auto &v : vertices)
		if (luts_graph[v].type == vertex_t::CELL)
			str += static_cast<char>(internal_representation.at(luts_graph[v])) + '0';
	return str;
}

void yosys_als::als_problem_t::compute_fitness()
{
	ff_values.erase(ff_values.begin(), ff_values.end());
	ff_values.push_back(compute_number_of_gates());
	ff_values.push_back(compute_error());
}

double yosys_als::als_problem_t::compute_number_of_gates() const
{
	size_t count = 0;
    for (auto &v : internal_representation)
        count += luts_catalog[get_lut_param(v.first.cell)][v.second].num_gates;
	return (double) count;
}

double yosys_als::als_problem_t::compute_error() const {
	return 0;
}

