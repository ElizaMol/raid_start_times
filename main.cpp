#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <locale>
#include <map>
//#include "string_view.hpp"

#if USE_GLIB_FOR_UTF8
#include <glib.h>
#endif

/*****************************************************************************/
// Debug macros

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#define DEBUG_LOG \
	std::cout
#else
#define DEBUG_LOG \
	if (0) std::cout
#endif

/*****************************************************************************/
// UTF related stuff

#if USE_GLIB_FOR_UTF8

/* Glib has own UTF implementation which seems to behave much better than gnu
 * libc I am lazy so I don't use in place checking. Also any right to left
 * issues are completly ignored in this function.
 */
size_t console_width(const std::string& line)
{
	glong items_read = 0;
	glong items_written = 0;
	GError* error = nullptr;
	auto ucs4string = g_utf8_to_ucs4(line.c_str(), line.size(), &items_read, &items_written, &error);
	if (ucs4string == nullptr)
	{
		return std::string::npos;
	}

	size_t width = 0;

	for (size_t position = 0; position < items_written; ++position)
	{
		if (g_unichar_iswide(ucs4string[position]))
		{
			width += 2;
			continue;
		}
		if (g_unichar_iszerowidth(ucs4string[position]))
		{
			continue;
		}
		width += 1;
	}
	g_free(ucs4string);
	return width;
}

#elif TRUST_WCSWIDTH

/* POSIX provides wcswidth, which should work correctly, but does not, and it
 * requires conversion from UTF-8 to wide characters.
 * Of coures we don't deal with right to left issues here too.
 */
size_t console_width(const std::string& line)
{
	wchar_t*  buffer = new wchar_t[line.size()];
	mbstowcs(buffer, line.c_str(), line.size());
	int width = wcswidth(buffer, line.size());
	delete[] buffer;
	if (width != -1 )
	{
		return width;
	}
	return std::string::npos;
}

#else // Lack of library support for UTF

size_t console_width(const std::string& /*line*/)
{
	return std::string::npos;
}

#endif // USE_GLIB_FOR_UTF8 | TRUST_WCSWIDT | Lack of library support for UTF

std::string spacer(const std::string& line, size_t position)
{
	if (position == std::string::npos)
	{
		position = line.size();
	}
	std::string to_measure = line.substr(0, position);
	size_t width = console_width(to_measure);
	if (width == std::string::npos)
	{
		return to_measure;
	}
	return std::string(width, ' ');
}

/*****************************************************************************/
// Parsing helpers

#define MARK_ERROR(line, position) \
	line << "\n" << spacer(line, position) << "^" << "\n"

#define POS_PRINT(line, position) \
	"position: " << position << "\n" << MARK_ERROR(line, position)


// no string_literals in C++11
//using namespace std::string_literals;

std::locale loc{""};

size_t next_not_white_position(const std::string& line, size_t position)
{
	while (position < line.size() && std::isspace(line[position], loc))
	{
		++position;
	}
	if (position == line.size())
	{
		position = std::string::npos;
	}
	return position;
}

size_t previous_not_white_position(const std::string& line, size_t position)
{
	while (position < line.size() && std::isspace(line[position], loc))
	{
		--position;
	}
	if (position == line.size())
	{
		position = std::string::npos;
	}
	return position;
}

bool is_comment(const std::string& line)
{
	if (line.empty())
	{
		return true;
	}
	if (line[0] == '#')
	{
		return true;
	}
	if (next_not_white_position(line, 0) == std::string::npos)
	{
		return true;
	}
	return false;
}

class parse_error : public std::runtime_error
{
public:
	parse_error(const size_t position, const std::string& what) :
		runtime_error(what),
		_position(position)
	{
	}
	size_t get_position()
	{
		return _position;
	}
private:
	const size_t _position;
};

int parse_int(const std::string& line, size_t& position, const std::string& name)
{
	size_t first_uncoverted = 0;
	int number=0;
	try
	{
		number = std::stoi(line.substr(position, line.size() - position), &first_uncoverted);
	}
	catch (std::exception& e)
	{
		throw parse_error(position + first_uncoverted, "Failed parsing of " + name + ", got following error: " + e.what());
	}
	position += first_uncoverted;
	return number;
	//DEBUG_LOG << "After parsing number\n" << POS_PRINT(line, position);
}

    template <typename int_type_T, typename converter_T>
void parse_list_of_integers(const std::string& line, size_t& position, std::vector<int_type_T>& integers, const std::string& name, const size_t end_position, converter_T converter)
{
		while (position < end_position)
		{
			const auto next_coma_or_closing_brace_position = line.find_first_of(",)", position);
			int value = parse_int(line, position, name);
			position = next_not_white_position(line, position);
			if (position != next_coma_or_closing_brace_position)
			{
				throw parse_error(position, "Garbage found after " + name);
			}
			int_type_T converted = converter(value);
			integers.push_back(converted);
			if (position < end_position)
			{
				++position;
			}
		}

		if (position != end_position)
		{
			throw parse_error(position, "Garbage found after last " + name);
		}
};

// Do nothing converter for parse_list_of_integers
int void_converter(int value)
{
	return value;
};

unsigned int parse_guild_activities_reset_time(const std::string& line, size_t& position)
{
	const auto colon_position = line.find_first_of(":", position);
	if (colon_position == std::string::npos)
	{
		throw parse_error(position, "There shall be colon in guild reset time");
	}

	int guild_activities_reset_reset_hour = parse_int(line, position, "guild activities reset time");
	if (guild_activities_reset_reset_hour < 0 || guild_activities_reset_reset_hour > 23)
	{
		throw parse_error(position, "Invalid guild reset hour, got " + std::to_string(guild_activities_reset_reset_hour) +
				" which is not reasonable --- should be not negative and smaller than 24");
	}

	position = next_not_white_position(line, position);
	if (colon_position != position)
	{
		throw parse_error(position, "Garbage found between end of guild reset hour and colon");
	}

	// skip colon
	++position;

	int minutes = parse_int(line, position, "minutes of guild activities reset time");
	if (minutes != 30)
	{
		throw parse_error(position, "Guild reset time must be exactly halfhour, but we found it at " + std::to_string(minutes) +
				" after whole hour, is in non-standard time-zone in use?");
	}

	return guild_activities_reset_reset_hour;
}

/*****************************************************************************/

class time_bitmap
{
public:
	unsigned int get_data() const
	{
		return data;
	}

	bool is_set(unsigned int hour) const
	{
		return data & (1u << hour);
	}

	void set(unsigned int hour)
	{
		data |= 1u << hour;
	}

	void unset(unsigned int hour)
	{
		data &= ~(1u << hour);
	}

	void out() const
	{
		for (unsigned int i = 0; i < 32; ++i)
		{
			if (is_set(i))
			{
				std::cout << i << " ";
			}
		}
	}

	time_bitmap operator&(const time_bitmap& other) const
	{
		time_bitmap retval;
		retval.data = data & other.data;
		return retval;
	}

	time_bitmap& operator^=(const time_bitmap& other)
	{
		data ^= other.data;
		return *this;
	}
private:
	unsigned int data = 0;
};

struct config_header
{

	unsigned int number_of_raid_times;
	unsigned int hour_of_master_activities_reset;
	std::vector<int> best_weights;
	std::vector<int> acceptable_weights;

	void out()
	{
		std::cout << "best\n";
		for (const auto& i : best_weights)
		{
			std::cout << i << "\n";
		}
		std::cout << "acceptable\n";
		for (const auto& i : acceptable_weights)
		{
			std::cout << i << "\n";
		}
	}

};

struct config_header_parser : config_header
{
	static const std::string raid_times_label;
	static const std::string guild_reset_label;
	static const std::string best_weights_label;
	static const std::string acceptable_weights_label;

	bool number_of_raid_times_parsed = false;
	bool hour_of_master_activities_reset_parsed = false;
	bool best_weights_parsed = false;
	bool acceptable_weights_parsed = false;

	bool parsed() const
	{
		return number_of_raid_times_parsed && hour_of_master_activities_reset_parsed && best_weights_parsed && acceptable_weights_parsed;
	}

	bool parse_label(const std::string& line, size_t& position, const std::string& label)
	{
		if (line.find(label, position) == position)
		{
			position = next_not_white_position(line, label.size());
			if (position == std::string::npos)
			{
				throw parse_error(raid_times_label.size(), "Expected something after label \"" + label + "\", but found only whitespace");
			}
		        //DEBUG_LOG << "After post label whitespace skip\n" << POS_PRINT(line, position);
			return true;
		}
		return false;
	}

	void check_for_end_of_line_garbage(const std::string& line, size_t& position)
	{
		position = next_not_white_position(line, position);
		if (position != std::string::npos)
		{
			throw parse_error(position, "Garbage found at the end of line");
		}
	}

	void parse(const std::string& line)
	{
		size_t position = next_not_white_position(line, 0);
		//DEBUG_LOG << "After whitespace skip\n" << POS_PRINT(line, position);;

		if (parse_label(line, position, raid_times_label))
		{
			parse_number_of_best_raid_times(line, position);
			number_of_raid_times_parsed = true;
		}
		else if (parse_label(line, position, guild_reset_label))
		{
			parse_hour_of_master_activities_reset(line, position);
			hour_of_master_activities_reset_parsed = true;
		}
		else if (parse_label(line, position, best_weights_label))
		{
			parse_list_of_integers(line, position, best_weights, "best weights", std::string::npos, void_converter);
			best_weights_parsed = true;
		}
		else if (parse_label(line, position, acceptable_weights_label))
		{
			parse_list_of_integers(line, position, acceptable_weights, "acceptable weights", std::string::npos, void_converter);
			acceptable_weights_parsed = true;
		}
		else
		{
			throw parse_error(0, "Unrecognized header line, we expect one of:\n" +
					raid_times_label + "\n" +
					guild_reset_label + "\n" +
					best_weights_label + "\n" +
					acceptable_weights_label + "\n"
					);
		}
	}

	void parse_number_of_best_raid_times(const std::string& line, size_t& position)
	{
		const int number = parse_int(line, position, "number of raid times to seek");
		if (number < 1 || number > 23)
		{
			throw parse_error(position, "Invalid number of raid times to seek, got " + std::to_string(number) +
					" which is not reasonable --- should be at least 1 and at most 23");
		}
		check_for_end_of_line_garbage(line, position);
		number_of_raid_times = number;
	}

	void parse_hour_of_master_activities_reset(const std::string& line, size_t& position)
	{
		hour_of_master_activities_reset = parse_guild_activities_reset_time(line, position);
		check_for_end_of_line_garbage(line, position);
	}
};

const std::string config_header_parser::raid_times_label = "Number of best raid times to seek:";
const std::string config_header_parser::guild_reset_label = "Guild activities reset time for results:";
const std::string config_header_parser::best_weights_label = "Best times weights list:";
const std::string config_header_parser::acceptable_weights_label = "Acceptable times weights list:";

struct player
{
	std::string name;
	bool guild_activities_reset_reset_hour_was_set = false;
	unsigned int hour_of_guild_activities_reset_in_player_time;
	time_bitmap best_times_in_master_time;
	time_bitmap acceptable_times_in_master_time;

	void out()
	{
		std::cout << name << ", best(";
		bool first_item_printed = false;
		for (unsigned int i = 0; i < 24; ++i)
		{
			if (best_times_in_master_time.is_set(i))
			{
				if (first_item_printed)
				{
					std::cout << ", ";
				}
				else
				{
					first_item_printed = true;
				}
				std::cout << i ;
			}
		}

		std::cout << "), acceptable(" ;
		first_item_printed = false;
		for (unsigned int i = 0; i < 24; ++i)
		{
			if (acceptable_times_in_master_time.is_set(i))
			{
				if (first_item_printed)
				{
					std::cout << ", ";
				}
				else
				{
					first_item_printed = true;
				}
				std::cout << i ;
			}
		}
		std::cout << ")\n";
	}
};

struct player_parser : player
{
	const std::string& line;
	const config_header& config;

	player_parser(const std::string& l, config_header& header) : line(l), config(header)
	{
		const auto first_coma_position = line.find_first_of(",");
		if (first_coma_position == std::string::npos)
		{
			throw parse_error(0, "There shall be coma after player name ");
		}

		name = line.substr(0, first_coma_position);

		DEBUG_LOG << "Getting data for player " << name << "\n";
		DEBUG_LOG << line.substr(first_coma_position + 1) << "\n";

		auto coma_position = first_coma_position;
		for (unsigned int i = 0; i < 3 && coma_position != std::string::npos; ++i)
		{
			coma_position = parse_single_command(coma_position);
		}
		if (coma_position != std::string::npos)
		{
			throw parse_error(coma_position, "Unexpected coma");
		}

		time_bitmap common_times = best_times_in_master_time & acceptable_times_in_master_time;
		//common_times.data = best_times_in_master_time.data & acceptable_times_in_master_time.data;
		if (common_times.get_data())
		{
			acceptable_times_in_master_time ^= common_times;
			std::cout << name << " has duplicate entry(ies) between best and acceptable list, removed from the latter: " ;
			for (unsigned int i = 0; i < 24; ++i)
			{
				if (common_times.is_set(i))
				{
					std::cout << i << " ";
				}
			}
			std::cout << "\n";
		}
	}

	size_t parse_single_command(const size_t pre_command_coma_position)
	{
		DEBUG_LOG << "Parsing single command \n";
		const auto opening_brace_position = line.find_first_of("(", pre_command_coma_position + 1);

		if (opening_brace_position == std::string::npos)
		{
			// Opening brace not found, but lets emit a bit different message
			throw parse_error(pre_command_coma_position, "There shall be \"reset(\" or \"best(\" or \"acceptable\" after coma");
		}

		const auto command_start = next_not_white_position(line, pre_command_coma_position + 1);
		const auto command_end = previous_not_white_position(line, opening_brace_position - 1);

		const auto closing_brace_position = line.find_first_of(")", opening_brace_position);
		if (closing_brace_position == std::string::npos)
		{
			throw parse_error(opening_brace_position, "There shall be closing brace after opening brace");
		}


		const std::string command = line.substr(command_start, command_end - command_start + 1);

		DEBUG_LOG << "command: \"" << command << "\"\n";
		DEBUG_LOG << "values: " << line.substr(opening_brace_position + 1, closing_brace_position - opening_brace_position - 1) << "\n";

		const auto first_non_whitespace = next_not_white_position(line, closing_brace_position + 1);
		const auto post_command_coma_position = line.find_first_of(",", first_non_whitespace);

		DEBUG_LOG << "initial coma" << POS_PRINT(line, pre_command_coma_position) ;
		DEBUG_LOG << "opening brace" << POS_PRINT(line, opening_brace_position) ;
		DEBUG_LOG << "closing brace " << POS_PRINT(line, closing_brace_position) ;
		DEBUG_LOG << "first not white" << POS_PRINT(line, first_non_whitespace) ;
		DEBUG_LOG << "following coma" << POS_PRINT(line, post_command_coma_position) ;

		// Note that if first_non_whitespace is npos, then post_command_coma_position will be npos too
		if (first_non_whitespace != post_command_coma_position)
		{
			throw parse_error(first_non_whitespace, "Found garbage after closing brace in command (is ther missing coma?)");
		}

		size_t position = opening_brace_position + 1;

		if (command == "reset")
		{
			hour_of_guild_activities_reset_in_player_time = parse_guild_activities_reset_time(line, position);
			guild_activities_reset_reset_hour_was_set = true;

			position = next_not_white_position(line, position);
			if (position != closing_brace_position)
			{
				throw parse_error(position, "Garbage found after guild reset time");
			}
		}
		else if (command == "best")
		{
			parse_list_of_times(position, best_times_in_master_time, "best time", closing_brace_position);
		}
		else if (command == "acceptable")
		{
			parse_list_of_times(position, acceptable_times_in_master_time, "acceptable time", closing_brace_position);
		}
		else
		{
			throw parse_error(command_start, "Unrecognized command: " + command);
		}

		return post_command_coma_position;
	}

	void parse_list_of_times(size_t& position, time_bitmap& times, const std::string& list_name, const size_t end_position)
	{
		DEBUG_LOG << "parsing list of " << list_name << "s for " << name << "\n";
		if (!guild_activities_reset_reset_hour_was_set)
		{
			throw parse_error(position, "List of " + list_name + "s found before guild activities reset time");
		}

		// If master time 18:30 is same as player time 19:30 then
		// to convert from player to guild we need to distract 1
		int diff = 24 + config.hour_of_master_activities_reset - hour_of_guild_activities_reset_in_player_time;

		auto convert_int_to_time = [diff, &position, &list_name] (int local_time_hour) -> unsigned int
		{
			if (local_time_hour < 0 || local_time_hour > 24)
			{
				throw parse_error(position, "Invalid " + list_name + " hour, got " + std::to_string(local_time_hour) +
						" which is not reasonable --- should be at least 0 and at most 24");
			}
			return (local_time_hour + diff) % 24;
		};

		std::vector<unsigned int> temporary_times;
		parse_list_of_integers(line, position, temporary_times, list_name, end_position, convert_int_to_time);

		for (const auto time : temporary_times)
		{
			times.set(time);
		}
	}

};

struct all_solutions_iterator
{
	std::vector<unsigned int> included;
	time_bitmap current;

	static all_solutions_iterator begin(unsigned int times)
	{
		return all_solutions_iterator(times);
	}

	static all_solutions_iterator end(unsigned int times)
	{
		auto retval = all_solutions_iterator();
		retval.included.resize(times);
		return retval;
	}

	all_solutions_iterator()
	{
	}

	all_solutions_iterator(unsigned int raid_times)
	{
		for (unsigned int i = raid_times - 1; i < 24; --i)
		{
			included.push_back(i);
			current.set(i);
		}
	}

	all_solutions_iterator& operator++()
	{
		if (!current.get_data())
		{
			return *this;
		}

		/* This is how it works:
		 * Times Bitmap i j
		 * 0 1 2 11100  0 -
		 * 0 1 3 11010  0 -
		 * 0 1 4 11001  0 -
		 * 0 1 5        1 -
		 * 0 2 5        1 0
		 * 0 2 3 10110  0 -
		 * 0 2 4 10101  0 -
		 * 0 2 5        1 -
		 * 0 3 5        1 0
		 * 0 3 4 10011  0 -
		 * 0 3 5        1 -
		 * 0 4 5        2 -
		 * 1 4 5        2 1
		 * 1 2 5        2 0
		 * 1 2 3 01110  0 -
		 * 1 2 4 01101  0 -
		 * 1 2 5        1 -
		 * 1 3 5        1 0
		 * 1 3 4 01011  0 -
		 * 1 3 5        1 -
		 * 1 4 5        2 -
		 * 2 4 5        2 1
		 * 2 3 5        2 0
		 * 2 3 4 00111  0 -
		 * 2 3 5        1 -
		 * 2 4 5        2 -
		 * 3 4 5        - - (after i loop)
		 * 3 4 5 00000
		 */
		for (unsigned int i = 0; i < included.size(); ++i)
		{
			current.unset(included[i]);
			++included[i];
			if (included[i] < (24 - i))
			{
				current.set(included[i]);
				for (unsigned int j = (i - 1); j < 24; --j)
				{
					included[j] = included[j + 1] + 1;
					current.set(included[j]);
				}
				return *this;
			}
		}
		current = time_bitmap();
		return *this;
	}

	const time_bitmap& operator*() const
	{
		return current;
	}

	bool operator==(const all_solutions_iterator& other) const
	{
		if (this == &other)
		{
			return true;
		}
		if (included.size() != other.included.size())
		{
			return false;
		}
		return current.get_data() == other.current.get_data();
	}

	bool operator!=(const all_solutions_iterator& other) const
	{
		return ! (*this == other);
	}

};

/* Taken from:
 * https://stackoverflow.com/questions/109023/how-to-count-the-number-of-set-bits-in-a-32-bit-integer
 */
unsigned int number_of_set_bits(unsigned int i)
{
#if USE_POP_COUNT
	return __builtin_popcount(i);
#else
	i = i - ((i >> 1) & 0x55555555u);
	i = (i & 0x33333333u) + ((i >> 2) & 0x33333333u);
	return (((i + (i >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
#endif
}

struct single_player_value_lookup_table
{
	std::vector<unsigned int> best;
	std::vector<unsigned int> acceptable;
};

long long solution_value(const time_bitmap& solution, const std::vector<player>& players, const single_player_value_lookup_table& values)
{
	long long value = 0;
	for (const auto& p : players)
	{
		const unsigned int best_times = number_of_set_bits((p.best_times_in_master_time & solution).get_data());
		const unsigned int acceptable_times = number_of_set_bits((p.acceptable_times_in_master_time & solution).get_data());
		value += values.best[best_times];
		value += values.acceptable[best_times + acceptable_times] - values.acceptable[best_times];
		DEBUG_LOG << p.name << " best(" << best_times << "), acceptable(" << acceptable_times << "), value(";
		DEBUG_LOG << values.best[best_times] << " + " << values.acceptable[best_times + acceptable_times] - values.acceptable[best_times] << ")\n";
	}
	return value;
}

long long solution_present(const time_bitmap& solution, const std::vector<player>& players, const single_player_value_lookup_table& values)
{
	long long value = 0;
	for (const auto& p : players)
	{
		const unsigned int best_times = number_of_set_bits((p.best_times_in_master_time & solution).get_data());
		const unsigned int acceptable_times = number_of_set_bits((p.acceptable_times_in_master_time & solution).get_data());
		std::cout << p.name << "(" << best_times << "|" << acceptable_times << ") (";
		std::cout << values.best[best_times] << " + " << values.acceptable[best_times + acceptable_times] - values.acceptable[best_times] << ")\n";
	}
	return value;
}

void calculate_results(const config_header& header, const std::vector<player>& players)
{
	DEBUG_LOG << " calculating results\n";
	single_player_value_lookup_table values;
	{
		unsigned int best_sum = 0;
		unsigned int acceptable_sum = 0;
		values.best.push_back(best_sum);
		values.acceptable.push_back(acceptable_sum);
		for (unsigned int i = 0; i < header.number_of_raid_times; ++i)
		{
			if (i < header.best_weights.size())
			{
				best_sum += header.best_weights[i];
			}
			else if (header.best_weights.empty() == false)
			{
				best_sum += header.best_weights.back();
			}
			if (i < header.acceptable_weights.size())
			{
				acceptable_sum += header.acceptable_weights[i];
			}
			else if (header.best_weights.empty() == false)
			{
				acceptable_sum += header.acceptable_weights.back();
			}
			values.best.push_back(best_sum);
			values.acceptable.push_back(acceptable_sum);
		}
	}

	/*
	24!
	----
	k!(24-k)!

	24| 1->     24
	24| 2->    276
	24| 3->   2024
	24| 4->  10626
	24| 5->  42504
	24| 6-> 134596
	24| 7-> 346104
	24| 8-> 735471
	24| 9->1307504
	24|10->1961256
	24|11->2496144
	24|12->2704156 (each solution: 4 bytes for hours, 8 bytes for value -> less than 64MB)
	*/
	std::multimap<long long, time_bitmap, std::greater<long long> > results;
	auto it = all_solutions_iterator::begin(header.number_of_raid_times);
	const auto end = all_solutions_iterator::end(header.number_of_raid_times);
	for (; it != end; ++it)
	{
		const auto& sol = *it;
		auto value = solution_value(sol, players, values);
		results.insert(std::make_pair(value, sol));
		if (DEBUG)
		{
			sol.out();
			DEBUG_LOG << ": " << value << "\n";
		}
	}

	long long best_value = results.begin()->first;

	unsigned int values_presented = 0;
	unsigned int max_values_to_present = 2048;

	unsigned int solutions_presented = 0;
	unsigned int max_solutions_to_present = 2048;

	for (const auto& sol : results)
	{
		if (sol.first < best_value)
		{
			++values_presented;
		}
		if (solutions_presented == max_solutions_to_present || values_presented == max_values_to_present)
		{
			break;
		}
		++solutions_presented;

		std::cout << sol.first << ":" ;
		sol.second.out();
		std::cout << "\n";
		solution_present(sol.second, players, values);
		std::cout << "\n";
	}
}

void remove_bom(std::string& line)
{
	if (line[0] == '\xef' && line[1] == '\xbb' && line[2] == '\xbf')
	{
		line = line.substr(3);
	}
}

int main()
{
	std::vector<player> players;
	config_header_parser header;
	unsigned int line_no = 0;
	std::string line;

	try
	{
		while (header.parsed() == false && std::getline(std::cin, line).good())
		{
			remove_bom(line);
			++line_no;
			if (is_comment(line))
			{
				continue;
			}
			header.parse(line);
		}

		std::cout << "Will try to find " << header.number_of_raid_times <<
			(header.number_of_raid_times > 1 ? " optimal raid times " : " optimal raid time ") <<
			"guild reset in timezone for results is at " << header.hour_of_master_activities_reset << ":30" << "\n";
		if (DEBUG) header.out();

		while (std::getline(std::cin, line).good())
		{
			remove_bom(line);
			++line_no;
			if (is_comment(line))
			{
				continue;
			}
			player_parser p(line, header);
			players.push_back(p);
			players.back().out();
		}

		calculate_results(header, players);
	}
	catch (parse_error& e)
	{
		size_t position = line.size();
		if (e.get_position() < line.size())
		{
			position = e.get_position();
		}
		std::cerr << "At line: " << line_no << ", " << POS_PRINT(line, position) << e.what() << "\n";
	}
	catch (std::runtime_error& e)
	{
		std::cerr << e.what() << std::endl;
	}
	catch (...)
	{
		std::cerr << "Oops at line " << line_no << std::endl;
	}
		//std::stoi(line, number_of_raid_times);
	return 0;
}
