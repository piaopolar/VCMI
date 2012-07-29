#pragma once

class JsonNode;
typedef std::map <std::string, JsonNode> JsonMap;
typedef std::vector <JsonNode> JsonVector;

struct Bonus;

class DLL_LINKAGE JsonNode
{
public:
	enum JsonType
	{
		DATA_NULL,
		DATA_BOOL,
		DATA_FLOAT,
		DATA_STRING,
		DATA_VECTOR,
		DATA_STRUCT
	};

private:
	union JsonData
	{
		bool Bool;
		double Float;
		std::string* String;
		JsonVector* Vector;
		JsonMap* Struct;
	};

	JsonType type;
	JsonData data;

public:
	//Create empty node
	JsonNode(JsonType Type = DATA_NULL);
	//Create tree from Json-formatted input
	explicit JsonNode(const char * data, size_t datasize);
	//Create tree from JSON file
 	explicit JsonNode(std::string filename);
	//Copy c-tor
	JsonNode(const JsonNode &copy);

	~JsonNode();

	void swap(JsonNode &b);
	JsonNode& operator =(JsonNode node);

	bool operator == (const JsonNode &other) const;
	bool operator != (const JsonNode &other) const;

	//removes all nodes that are identical to default entry in schema
	void minimize(const JsonNode& schema);
	//check schema
	void validate(const JsonNode& schema);

	//Convert node to another type. Converting to NULL will clear all data
	void setType(JsonType Type);
	JsonType getType() const;

	bool isNull() const;

	//non-const accessors, node will change type on type mismatch
	bool & Bool();
	double & Float();
	std::string & String();
	JsonVector & Vector();
	JsonMap & Struct();

	//const accessors, will cause assertion failure on type mismatch
	const bool & Bool() const;
	const double & Float() const;
	const std::string & String() const;
	const JsonVector & Vector() const;
	const JsonMap & Struct() const;

	template<typename T>
	std::vector<T> StdVector() const
	{
		static_assert(std::is_arithmetic<T>::value, "This works with numbers only.");
		std::vector<T> ret;
		BOOST_FOREACH(const JsonNode &node, Vector())
		{
			ret.push_back(node.Float());
		}
		return ret;
	}

	//operator [], for structs only - get child node by name
	JsonNode & operator[](std::string child);
	const JsonNode & operator[](std::string child) const;

	//error value for const operator[]
	static const JsonNode nullNode;
};

class JsonWriter
{
	//prefix for each line (tabulation)
	std::string prefix;
	std::ostream &out;
public:
	template<typename Iterator>
	void writeContainer(Iterator begin, Iterator end);
	void writeEntry(JsonMap::const_iterator entry);
	void writeEntry(JsonVector::const_iterator entry);
	void writeString(const std::string &string);
	void writeNode(const JsonNode &node);
	JsonWriter(std::ostream &output, const JsonNode &node);
};

DLL_LINKAGE std::ostream & operator<<(std::ostream &out, const JsonNode &node);

//Tiny string class that uses const char* as data for speed, members are private
//for ease of debugging and some compatibility with std::string
class constString
{
	const char *data;
	const size_t datasize;

public:
	constString(const char * inputString, size_t stringSize):
		data(inputString),
		datasize(stringSize)
	{
	}

	inline size_t size() const
	{
		return datasize;
	};

	inline const char& operator[] (size_t position)
	{
		assert (position < datasize);
		return data[position];
	}
};

//Internal class for string -> JsonNode conversion
class JsonParser
{
	std::string errors;     // Contains description of all encountered errors
	constString input;      // Input data
	ui32 lineCount; // Currently parsed line, starting from 1
	size_t lineStart;       // Position of current line start
	size_t pos;             // Current position of parser

	//Helpers
	bool extractEscaping(std::string &str);
	bool extractLiteral(const std::string &literal);
	bool extractString(std::string &string);
	bool extractWhitespace(bool verbose = true);
	bool extractSeparator();
	bool extractElement(JsonNode &node, char terminator);

	//Methods for extracting JSON data
	bool extractArray(JsonNode &node);
	bool extractFalse(JsonNode &node);
	bool extractFloat(JsonNode &node);
	bool extractNull(JsonNode &node);
	bool extractString(JsonNode &node);
	bool extractStruct(JsonNode &node);
	bool extractTrue(JsonNode &node);
	bool extractValue(JsonNode &node);

	//Add error\warning message to list
	bool error(const std::string &message, bool warning=false);

public:
	JsonParser(const char * inputString, size_t stringSize, JsonNode &root);
};

//Internal class for Json validation, used automaticaly in JsonNode constructor. Behaviour:
// - "schema" entry from root node is used for validation and will be removed
// - any missing entries will be replaced with default value from schema (if present)
// - if entry uses different type than defined in schema it will be removed
// - entries nod described in schema will be kept unchanged
class JsonValidator
{
	std::string errors;     // Contains description of all encountered errors
	std::list<std::string> currentPath; // path from root node to current one
	bool minimize;

	bool validateType(JsonNode &node, const JsonNode &schema, JsonNode::JsonType type);
	bool validateSchema(JsonNode::JsonType &type, const JsonNode &schema);
	bool validateNode(JsonNode &node, const JsonNode &schema, const std::string &name);
	bool validateItems(JsonNode &node, const JsonNode &schema);
	bool validateProperties(JsonNode &node, const JsonNode &schema);

	bool addMessage(const std::string &message);
public:
	// validate node with "schema" entry
	JsonValidator(JsonNode &root, bool minimize=false);
	// validate with external schema
	JsonValidator(JsonNode &root, const JsonNode &schema, bool minimize=false);
};

DLL_LINKAGE Bonus * ParseBonus (const JsonVector &ability_vec);
