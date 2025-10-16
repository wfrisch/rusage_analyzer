#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <numeric>
#include <cassert>
#include <chrono>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>


struct GlobalConfig {
	int sample_count = 100;
	double alpha = 0.025;
	int interleave = 100;
};

struct ScenarioConfig {
	std::string label;
	std::vector<std::string> cmd;
	std::map<std::string, std::string> env;

	bool is_defined() const {
		return !cmd.empty();
	}
};

struct Sample {
	rusage rusage_data;
	double elapsed_us;
};

//const std::vector<std::string> RUSAGE_FIELDS = {
//	"ru_utime", "ru_stime", "ru_ixrss", "ru_idrss", "ru_isrss", "ru_minflt",
//	"ru_majflt", "ru_nswap", "ru_inblock", "ru_oublock", "ru_msgsnd",
//	"ru_msgrcv", "ru_nsignals", "ru_nvcsw", "ru_nivcsw", "ru_maxrss"
//};
// removed unused fields, see getrusage(2)
const std::vector<std::string> RUSAGE_FIELDS = {
	"ru_utime", "ru_stime", "ru_minflt", "ru_majflt", "ru_inblock",
	"ru_oublock", "ru_nvcsw", "ru_nivcsw", "ru_maxrss"
};

using Samples = std::vector<Sample>;
using PreparedData = std::map<std::string, std::vector<double>>;

namespace stats {
	double calculate_median(std::vector<double> data);
	double mann_whitney_u_test(const std::vector<double>& group_a, const std::vector<double>& group_b);
}

double timeval_to_us(const timeval& tv) {
	return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) / 1'000'000.0;
}

double get_rusage_field(const rusage& r, const std::string& field) {
	if (field == "ru_utime") return timeval_to_us(r.ru_utime);
	if (field == "ru_stime") return timeval_to_us(r.ru_stime);
	if (field == "ru_ixrss") return static_cast<double>(r.ru_ixrss);
	if (field == "ru_idrss") return static_cast<double>(r.ru_idrss);
	if (field == "ru_isrss") return static_cast<double>(r.ru_isrss);
	if (field == "ru_minflt") return static_cast<double>(r.ru_minflt);
	if (field == "ru_majflt") return static_cast<double>(r.ru_majflt);
	if (field == "ru_nswap") return static_cast<double>(r.ru_nswap);
	if (field == "ru_inblock") return static_cast<double>(r.ru_inblock);
	if (field == "ru_oublock") return static_cast<double>(r.ru_oublock);
	if (field == "ru_msgsnd") return static_cast<double>(r.ru_msgsnd);
	if (field == "ru_msgrcv") return static_cast<double>(r.ru_msgrcv);
	if (field == "ru_nsignals") return static_cast<double>(r.ru_nsignals);
	if (field == "ru_nvcsw") return static_cast<double>(r.ru_nvcsw);
	if (field == "ru_nivcsw") return static_cast<double>(r.ru_nivcsw);
	if (field == "ru_maxrss") return static_cast<double>(r.ru_maxrss);
	return 0.0;
}


// The output vector `samples` should be reserved appropriately to avoid memory
// allocation in the collection loop, which could distort the results,
// particulary max_rss, which is inherited from this process.
// see: https://jkz.wtf/random-linux-oddity-1-ru_maxrss
void collect_samples(const ScenarioConfig& scenario, int count, Samples& samples) {
	std::cout << "[*] Collecting " << count << " samples for scenario: '" 
		<< scenario.label << "'..." << std::endl;
   
	// for execvp
	std::vector<const char*> cmd_c_str;
	for (const auto& arg : scenario.cmd) {
		cmd_c_str.push_back(arg.c_str());
	}
	cmd_c_str.push_back(nullptr);

	for (int i = 0; i < count; ++i) {
		pid_t pid = fork();

		if (pid == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if (pid == 0) { // child process
			for(const auto& [k,v] : scenario.env) {
				if (setenv(k.c_str(), v.c_str(), 1) != 0) {
					perror("setenv");
					exit(EXIT_FAILURE);
				}
			}
			
			// silence child
			int dev_null = open("/dev/null", O_WRONLY);
			if (dev_null == -1) exit(EXIT_FAILURE);
			dup2(dev_null, STDOUT_FILENO);
			dup2(dev_null, STDERR_FILENO);
			close(dev_null);

			execvp(cmd_c_str[0], const_cast<char* const*>(cmd_c_str.data()));
			perror("execvp");
			exit(EXIT_FAILURE);
		} else { // parent process
			int status;
			rusage rusage_data;
			// wall clock is not measured by getrusage()
			auto time_start = std::chrono::high_resolution_clock::now();
			// wait4 is preferred as it can wait for a specific child PID
			if (wait4(pid, &status, 0, &rusage_data) != -1) {
				auto time_end = std::chrono::high_resolution_clock::now();
				auto time_elapsed = time_end - time_start;
				double elapsed_us = double(std::chrono::duration_cast<std::chrono::microseconds>(time_elapsed).count());
				samples.push_back({rusage_data, elapsed_us});
				//std::cout << "\r	" << (i + 1) << "/" << count << std::flush;
				std::cout << "." << std::flush;
			} else {
				perror("wait4");
			}
		}
	}

	std::cout << "\n[*] Collection complete.\n" << std::endl;
}

/**
 * @brief Converts a list of samples into a map of lists for each field.
 */
PreparedData prepare_data(const Samples& samples) {
	PreparedData data;
	for (const auto& r : samples) {
		for (const auto& field : RUSAGE_FIELDS) {
			data[field].push_back(get_rusage_field(r.rusage_data, field));
		}
		data["elapsed_us"].push_back(r.elapsed_us);
	}
	return data;
}

/**
 * @brief Performs the statistical analysis and prints the report.
 */
void analyze_results(const GlobalConfig& g_config, PreparedData& data_a, PreparedData& data_b, const std::string& label_a, const std::string& label_b) {
	struct Result {
		std::string field;
		double p_value;
		double median_a;
		double median_b;
		bool is_significant;
	};

	std::vector<Result> results;
	for (const auto& [field, _value] : data_a) {
		auto& group_a = data_a[field];
		auto& group_b = data_b[field];

		double median_a = stats::calculate_median(group_a);
		double median_b = stats::calculate_median(group_b);
		double p_value = stats::mann_whitney_u_test(group_a, group_b);
		
		results.push_back({
			field,
			p_value,
			median_a,
			median_b,
			p_value < g_config.alpha && median_a != median_b
		});
	}

	// Sort results by p-value to show most significant first
	std::sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
		return a.p_value < b.p_value;
	});

	// Print report
	std::cout << std::string(75, '-') << std::endl;
	std::cout << "Side-Channel Analysis Report" << std::endl;
	std::cout << std::string(75, '-') << std::endl;
	std::cout << std::left << std::setw(15) << "Metric" << " | "
	          << std::setw(12) << "p-value" << " | "
	          << std::setw(12) << "Median A" << " | "
	          << std::setw(12) << "Median B" << " | "
	          << std::setw(12) << "Significant" << std::endl;
	std::cout << std::left << std::setw(15) << "" << " | "
	          << std::setw(12) << "" << " | "
	          << std::setw(12) << "(" + label_a + ")" << " | "
	          << std::setw(12) << "(" + label_b + ")" << " | "
	          << "(alpha=" << g_config.alpha << ")" << std::endl;
	std::cout << std::string(75, '-') << std::endl;

	for (const auto& res : results) {
		std::cout << std::left << std::setw(15) << res.field << " | "
		          << std::scientific << std::setprecision(5) << std::setw(12) << res.p_value << " | "
		          << std::fixed << std::setprecision(2) << std::setw(12) << res.median_a << " | "
		          << std::setw(12) << res.median_b << " | "
		          << std::setw(12) << (res.is_significant ? "YES" : "no") << std::endl;
	}
	std::cout << std::string(75, '-') << std::endl;

	// Find and report the best predictor
	auto best_it = std::find_if(results.begin(), results.end(), [](const Result& r){ return r.is_significant; });
	if (best_it != results.end()) {
		std::cout << "\n[+] Conclusion: The best predictor is '" << best_it->field << "'." << std::endl;
		std::cout << "    You can likely distinguish between the two scenarios by observing this value." << std::endl;
	} else {
		std::cout << "\n[-] Conclusion: No statistically significant predictor found with the current data." << std::endl;
		std::cout << "    Try increasing SAMPLE_COUNT or check if the scenarios have different resource footprints." << std::endl;
	}
}


void print_usage(const char* prog_name) {
	std::cerr << "Usage: " << prog_name << " [GLOBAL_OPTIONS] --scenario-a [OPTS] -- <COMMAND_A> --scenario-b [OPTS] -- <COMMAND_B>\n\n"
	          << "Global Options:\n"
	          << "  -n <N>                    Number of samples to collect (default: 100)\n"
	          << "  -a <A>                    Significance level for the U-test (default: 0.05)\n\n"
	          << "Scenario Options (must be placed after --scenario-a or --scenario-b):\n"
	          << "  -l <STRING>               A descriptive label for the scenario in the report\n"
	          << "  -e <KEY=VALUE>            Set an environment variable (can be used multiple times)\n\n"
	          << "The '--' separator is mandatory before each command.\n\n"
	          << "Example:\n"
	          << "  " << prog_name << " -n 200 --scenario-a -l 'C Locale' -e LC_ALL=C -- /usr/bin/grep 'word' file.txt \\\n"
	          << "          --scenario-b -l 'UTF8 Locale' -e LC_ALL=en_US.UTF-8 -- /usr/bin/grep 'word' file.txt\n";
}


int main(int argc, char** argv) {
	GlobalConfig g_config;
	ScenarioConfig scenario_a, scenario_b;

	try {
		ScenarioConfig* current_scenario = nullptr;

		for (int i = 1; i < argc; i++) {
			std::string arg = argv[i];

			if (arg == "--help" || arg == "-h") {
				print_usage(argv[0]);
				return 0;
			} else if (arg == "--scenario-a") {
				current_scenario = &scenario_a;
			} else if (arg == "--scenario-b") {
				current_scenario = &scenario_b;
			} else if (arg == "-n") {
				g_config.sample_count = std::stoi(argv[++i]);
			} else if (arg == "-a") {
				g_config.alpha = std::stod(argv[++i]);
			} else if (arg == "-l") {
				if (!current_scenario)
					throw std::runtime_error("Option " + arg + " must follow a scenario flag.");
				current_scenario->label = argv[++i];
			} else if (arg == "-e") {
				if (!current_scenario)
					throw std::runtime_error("Option " + arg + " must follow a scenario flag.");
				std::string env_str = argv[++i];
				size_t pos = env_str.find('=');
				if (pos == std::string::npos || pos == 0)
					throw std::runtime_error("Invalid env format: " + env_str);
				current_scenario->env[env_str.substr(0, pos)] = env_str.substr(pos + 1);
			} else if (arg == "--") {
				if (!current_scenario)
					throw std::runtime_error("Option " + arg + " must follow a scenario flag.");
				while (++i < argc && std::string(argv[i]) != "--scenario-a" && std::string(argv[i]) != "--scenario-b") {
					current_scenario->cmd.push_back(argv[i]);
                }
				--i;
			} else {
				throw std::runtime_error("Unknown option: " + arg);
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "[!] Error parsing arguments: " << e.what() << std::endl;
		print_usage(argv[0]);
		return 1;
	}

	if (!scenario_a.is_defined() || !scenario_b.is_defined()) {
		std::cerr << "[!] Error: Both scenario A and B must be fully defined." << std::endl;
		print_usage(argv[0]);
		return 1;
	}
	if (scenario_a.label.empty()) scenario_a.label = "Scenario A";
    if (scenario_b.label.empty()) scenario_b.label = "Scenario B";

	// Check if the target program is executable
	if (access(scenario_a.cmd[0].c_str(), X_OK) != 0) {
		std::cerr << "[!] Error: Target program '" << scenario_a.cmd[0] << "' not found or not executable." << std::endl;
		std::cerr << "    Please create it and run 'chmod +x target_program.sh'." << std::endl;
		return 1;
	}

	// 1. Collect data for both scenarios
	Samples samples_a, samples_b;
	samples_a.reserve(g_config.sample_count);
	samples_b.reserve(g_config.sample_count);

	// interleave sample collection to reduce environmental effects
	for (int i = 0; i < g_config.sample_count; i += g_config.interleave) {
		collect_samples(scenario_a, g_config.interleave, samples_a);
		collect_samples(scenario_b, g_config.interleave, samples_b);
	}

	if (samples_a.empty() || samples_b.empty()) {
		std::cerr << "[!] Error: Failed to collect sufficient samples. Exiting." << std::endl;
		return 1;
	}

	// 2. Prepare the data for analysis
	PreparedData data_a = prepare_data(samples_a);
	PreparedData data_b = prepare_data(samples_b);

	// 3. Analyze and print the report
	analyze_results(g_config, data_a, data_b, scenario_a.label, scenario_b.label);

	return 0;
}


namespace stats {
	/**
	 * @brief Calculates the median of a vector of doubles.
	 */
	double calculate_median(std::vector<double> data) {
		if (data.empty()) return 0.0;
		
		size_t n = data.size();
		std::sort(data.begin(), data.end());

		if (n % 2 == 0) {
			return (data[n / 2 - 1] + data[n / 2]) / 2.0;
		} else {
			return data[n / 2];
		}
	}

	/**
	 * @brief Performs a two-sided Mann-Whitney U Test.
	 * Uses normal approximation with tie correction to calculate the p-value.
	 * @return The two-sided p-value.
	 */
	double mann_whitney_u_test(const std::vector<double>& group_a, const std::vector<double>& group_b) {
		const double n1 = group_a.size();
		const double n2 = group_b.size();

		if (n1 == 0 || n2 == 0) return 1.0;

		// Combine samples and assign ranks
		std::vector<std::pair<double, int>> combined;
		combined.reserve(n1 + n2);
		for (const auto& val : group_a) combined.push_back({val, 1}); // Group 1
		for (const auto& val : group_b) combined.push_back({val, 2}); // Group 2

		std::sort(combined.begin(), combined.end());

		std::vector<double> ranks(n1 + n2);
		double rank_sum_a = 0.0;
		double tie_correction_sum = 0.0;

		for (size_t i = 0; i < combined.size(); ) {
			size_t j = i;
			while (j < combined.size() && combined[j].first == combined[i].first) {
				j++;
			}
			// Group of ties found from index i to j-1
			double rank = (i + 1 + j) / 2.0;
			double tie_count = j - i;
			if (tie_count > 1) {
				tie_correction_sum += (std::pow(tie_count, 3) - tie_count);
			}

			for (size_t k = i; k < j; ++k) {
				ranks[k] = rank;
				if (combined[k].second == 1) {
					rank_sum_a += rank;
				}
			}
			i = j;
		}

		// Calculate U statistic
		const double u1 = rank_sum_a - (n1 * (n1 + 1.0)) / 2.0;
		const double u2 = (n1 * n2) - u1;
		const double u_stat = std::min(u1, u2);

		// Calculate p-value using normal approximation
		const double mean_u = (n1 * n2) / 2.0;

		// If all values are identical, variance is 0
		if (tie_correction_sum == (std::pow(n1 + n2, 3) - (n1 + n2))) {
			return 1.0;
		}

		const double n_total = n1 + n2;
		const double variance_u = ((n1 * n2) / (n_total * (n_total - 1.0))) * ((std::pow(n_total, 3) - n_total) / 12.0 - tie_correction_sum / 12.0);
		
		if (variance_u <= 0) return 1.0;
		
		const double std_dev_u = std::sqrt(variance_u);

		// Apply continuity correction
		const double z_score = (u_stat - mean_u + 0.5) / std_dev_u;
		
		// p-value from Z-score (two-tailed) using the error function
		// P(Z <= z) = 0.5 * (1 + erf(z / sqrt(2)))
		const double cdf = 0.5 * (1.0 + std::erf(z_score / std::sqrt(2.0)));
		return 2.0 * std::min(cdf, 1.0 - cdf);
	}
}
