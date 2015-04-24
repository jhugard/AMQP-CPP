#pragma once

#include <string>
#include <strstream>

class Formatter
{
public:
	Formatter() {}
	~Formatter() {}

	template <typename Type>
	Formatter & operator << (const Type & value)
	{
		stream_ << value;
		return *this;
	}

	std::string str()		{ return stream_.str(); }
	operator std::string()	{ return stream_.str(); }

	enum ConvertToString
	{
		to_str
	};
	std::string operator >> (ConvertToString) { return stream_.str(); }

private:
	std::strstream stream_;

	Formatter(const Formatter &);
	Formatter & operator = (Formatter &);
};
