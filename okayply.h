#pragma once

// ----------------------------------------------
// 
// okayply - An okay ply C++ reader/writer
// 
// Alpha Version 1.1
// 
// license: MIT, see okayply/LICENSE
// 
// ----------------------------------------------

#include <unordered_map>
#include <string_view>
#include <functional>
#include <typeindex>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <array>
#include <span>
#include <any>

#define USE_CRLF_LFCR_HEADER_HACK

namespace okayply
{

	enum class format : std::uint8_t
	{
		ascii, binary
	};
	struct prop;
	struct elem;
	struct root;
	struct type;

	namespace str
	{
		// default ply 1.0 types
		inline constexpr auto t_char = "char";
		inline constexpr auto t_int8 = "int8";
		inline constexpr auto t_uchar = "uchar";
		inline constexpr auto t_uint8 = "uint8";
		inline constexpr auto t_short = "short";
		inline constexpr auto t_int16 = "int16";
		inline constexpr auto t_ushort = "ushort";
		inline constexpr auto t_uint16 = "uint16";
		inline constexpr auto t_int = "int";
		inline constexpr auto t_int32 = "int32";
		inline constexpr auto t_uint = "uint";
		inline constexpr auto t_uint32 = "uint32";
		inline constexpr auto t_float = "float";
		inline constexpr auto t_float32 = "float32";
		inline constexpr auto t_double = "double";
		inline constexpr auto t_float64 = "float64";

		// other names
		inline constexpr auto elem = "element";
		inline constexpr auto prop = "property";
		inline constexpr auto list = "list";
		inline constexpr auto comment = "comment";
		inline constexpr auto ply = "ply";
		inline constexpr auto format = "format";
		inline constexpr auto end_header = "end_header";
		inline constexpr auto ascii = "ascii";
		inline constexpr auto binary_little_endian = "binary_little_endian";
		inline constexpr auto binary_big_endian = "binary_big_endian";
		inline constexpr auto version = "1.0";
		inline constexpr auto cr = '\r'; // definition says: use \r but most implementations use \n
		inline constexpr auto lf = '\n'; // definition says: use \r but most implementations use \n
		inline constexpr auto space = ' ';
		inline constexpr auto invalidSymbols = "\n\v\f\r"; // do not use these in comments or names
		inline constexpr auto ignoreLineSymbols = "{#;~([|"; // everything in a line after these symbols will be ignored when reading the header or ascii
		inline constexpr auto f32fmt = "{:.9}";
		inline constexpr auto f64fmt = "{:.17}";
	}

	namespace internal
	{
		template<format ff, std::endian ee>
		inline constexpr std::string_view formatName()
		{
			if constexpr(ff == format::ascii)
				return str::ascii;
			if constexpr(ff == format::binary && ee == std::endian::little)
				return str::binary_little_endian;
			if constexpr(ff == format::binary && ee == std::endian::big)
				return str::binary_big_endian;
		}
		inline constexpr std::string_view listIndexTypeName(std::uint8_t i)
		{
			switch(i)
			{
			case 1: return str::t_uchar;
			case 2: return str::t_ushort;
			case 4: return str::t_uint;
			default: throw std::runtime_error("invalid list index type");
			}
		}

		template<std::uint8_t i>
		using uintX = 
            std::conditional_t<i == 1, std::uint8_t,
            std::conditional_t<i == 2, std::uint16_t,
            std::conditional_t<i == 4, std::uint32_t,
            std::uint64_t>>>;
		struct ErasedInfoBase
		{
			virtual bool            isList() const = 0;
			virtual std::type_index tid() const = 0;
			virtual std::size_t     typeSize() const = 0;
			virtual std::type_index vecTid() const = 0;
			virtual std::uint8_t    listIndexTypeSize(std::any const & any) const = 0;
			
            virtual void*           vecPtr(std::any& any) const = 0; // pointer to vector<T>
			virtual const void*     vecPtr(std::any const & any) const = 0; // pointer to vector<T>
			
            virtual void*           rawPtr(std::any& any) const = 0; // pointer to element [0] of vector<T>
			virtual const void*     rawPtr(std::any const & any) const = 0; // pointer to element [0] of vector<T>

			virtual ~ErasedInfoBase() = default;
		};

		template<typename T, bool is_list>
		struct ErasedInfo : public ErasedInfoBase
		{
			std::type_index vecTid() const override
			{
				return typeid(std::vector<T>);
			}
			std::type_index tid() const override
			{
				return typeid(T);
			}
			std::size_t typeSize() const override
			{
				return sizeof(T);
			}
			bool isList() const override
			{
				return is_list;
			}
			std::uint8_t listIndexTypeSize(
                std::any const & any) const override
			{
				if constexpr(is_list)
				{
					auto const & vv = std::any_cast<std::vector<std::vector<T>>const &>(any);
					std::size_t max = 0;
					for(auto & v : vv)
						max = std::max(max, v.size());
					return max < 256
                        ? 1
                        : max < 65536
                            ? 2
                            : 3;
				}
				else if constexpr(!is_list)
				{
					return 0;
				}
			}
			void* vecPtr(
                std::any& any) const override
			{
				// It depends on the std::any implementation if this is needed. If
				// sizeof(std::vector<T>) fits into the small storage of std::any,
				// the adresses of &std::vector<T> and &std::any are equal. If it
				// does not fit, the vector gets heap allocated and has a different adresss.
				if constexpr(is_list)
                    return reinterpret_cast<void*>(&std::any_cast<std::vector<std::vector<T>>&>(any));
				else
                    return reinterpret_cast<void*>(&std::any_cast<std::vector<T>&>(any));
			}
			const void* vecPtr(
                const std::any& any) const override
			{
				if constexpr(is_list)
                    return reinterpret_cast<const void*>(&std::any_cast<std::vector<std::vector<T>>const &>(any));
				else
                    return reinterpret_cast<const void*>(&std::any_cast<std::vector<T>const &>(any));
			}
			void* rawPtr(
                std::any& any) const override
			{
				if constexpr(is_list)
                    return reinterpret_cast<void*>(std::any_cast<std::vector<std::vector<T>>&>(any).data());
				else
                    return reinterpret_cast<void*>(std::any_cast<std::vector<T>&>(any).data());
			}
			const void* rawPtr(
                const std::any& any) const override
			{
				if constexpr(is_list)
                    return reinterpret_cast<const void*>(std::any_cast<std::vector<std::vector<T>>const &>(any).data());
				else
                    return reinterpret_cast<const void*>(std::any_cast<std::vector<T>const &>(any).data());
			}
		};
		inline std::uint32_t getline(
            std::istream& in,
            std::string& line,
            char& character)
		{
			// getline with \r and \n (or both) as line seperators
			// and without leading and trailing whitespace
			// and without empty lines
			// and with tracking of \r and \n usage
			std::uint32_t crlf = 0;
			line.clear();

			auto sanitize = [&character, &line] () {
				std::size_t start = 0;
				while(start < line.size() && line[start] == str::space)
                    start++;

				std::size_t end = std::min(line.find_first_of(str::ignoreLineSymbols), line.size());
				while(end > start && line[end - 1] == str::space)
                    end--;

				line = line.substr(start, end - start);
			};

			while(in.get(character))
			{
				if(character == str::cr || character == str::lf)
				{
					crlf += character == str::cr
                        ? 0x00010000
                        : 0x00000001; // track how often cr and lf are used
					sanitize();
					if(!line.size())
                        continue;
					return crlf;
				}
				line += character;
			}
			sanitize();
			return crlf;
		}
		inline std::vector<std::string> split(
            const std::string& s,
            char delim)
		{
			std::vector<std::string> r;
			std::stringstream ss(s);
			std::string token;
			while(std::getline(ss, token, delim))
				r.push_back(token);
			return r;
		}
	}

	// Custom data types can be registered via
	// template<typename T, bool isList> struct CustomIO : public IO
	// and registerType<type, CustomIO>();
	// see IO_X later in the code.
	struct type
	{
		// Serialize & Deserialize
		virtual void ascI(
            std::istream&,
            void*,
            std::size_t) const = 0;
		virtual void ascO(
            std::ostream&,
            const void*,
            std::size_t) const = 0;
		virtual void binI(
            std::istream&,
            void*,
            std::size_t,
            std::uint8_t,
            bool) const = 0;
		virtual void binO(
            std::ostream&,
            const void*,
            std::size_t,
            std::uint8_t,
            bool) const = 0;

		// First name will be used when writing, all names are valid for reading
		virtual std::vector<std::string_view> names() const = 0;

		// Do not touch
		virtual ~type() = default;
	};

	struct prop
	{
		friend root; // friends are nice.
		friend elem;

        // how many datapoints are in the property?
		std::size_t size() const;

        // whats my name?
		std::string_view name() const;

        // mutable data access
		template<typename T> std::span<T> get();

        // set data
		template<typename T> void set(std::span<T const>);
		template<typename T> void set(std::vector<T> const &);

        // gets the type of the property
		std::type_index type() const;

        // for lists, this is std::vector<type>, for non lists, this is equal to type()
		std::type_index listType() const;

         // is true if the property is a property list
		bool isList() const;

        // start adress of data
		void* rawPtr();

        // data size in bytes
		std::size_t rawSize();

	private:
		std::any        data_;
		elem*           parent_ = nullptr;
		std::type_index tid_ = typeid(void);
		std::uint8_t    listIndexSize_ = 0; // only used when reading files
	};

	struct elem
	{
		friend root;
		friend prop;

        // access only, returns element that matches the earliest name in the list
		prop& operator()(
            std::span<const std::string_view>);
		// full initialisation
        prop& operator()(
            std::string_view,
            const std::type_index&);
        // partial initialisation (full after first Property.get<T>())
		prop& operator()(
            std::string_view);
        
        // check if property exists
		bool has(std::string_view) const;
        
        // get the number of elements
		std::size_t size() const;
        
        // get the name of the element
		std::string_view name() const;
        
        // get all the properties from this element
		std::vector<std::reference_wrapper<prop>> properties();
        
        // get all names of the properties
		std::vector<std::string> names() const;
        
        // delete a property by name
		void del(std::string_view);

	private:
		template<
            format ff = format::ascii,
            std::endian ee = std::endian::native>
		void read(std::istream &);

		template<
            format ff = format::ascii,
            std::endian ee = std::endian::native>
		void write(std::ostream &) const;

		std::unordered_map<
            const prop*,
            std::string>            names_;
		std::vector<std::string>    order_;
		root*                       parent_ = nullptr;

		std::unordered_map<
            std::string,
            prop>                   properties_;

		std::size_t                 size_ = 0;
	};

	struct root
	{
		friend elem;
		friend prop;

		root();

        // full init
		elem& operator()(
            std::string_view,
            std::size_t);
        // access only after init
		elem& operator()(
            std::string_view);
        // const access
		const elem& operator()(
            std::string_view) const;

        // get all the comments and manage them yourself
		std::vector<std::string>& comments();

        // load a file (do not forget std::ios::binary!)
		void read(
            std::istream&);
        // load a file
		void read(
            const std::string&);
        
        // add a custom datatype
		template<
            typename T,
            template<typename, bool> typename CustomIO>
        void registerType();
		
        // write a file to utput stream
        template<
            format ff = format::ascii,
            std::endian ee = std::endian::native>
		void write(
            std::ostream &) const;

        // write a file
		template<
            format ff = format::ascii,
            std::endian ee = std::endian::native>
        void write(
            const std::string&) const;
		
        // get ascii representation of the ply
        std::string str() const;
		
        // old line seperator = lineSeperator(new line seperator)
        char lineSeperator(char);
		
        // get all the elements
        std::vector<std::reference_wrapper<elem>> elements();
		
        // get all element names
        std::vector<std::string> names() const;
		
        // delete an element by name
        void del(std::string_view);
		
        // check if element exists
        bool has(std::string_view) const;
		
        // check if element with property exists
        bool has(
            std::string_view,
            std::string_view) const;

	private:
		std::type_index typeidFromStr(
            std::string_view,
            bool);
		
        char linesep_ = str::lf;

        std::unordered_map<
            std::type_index,
            std::function<
                std::any(std::size_t)>>     anyvec_;
		std::vector<std::string>            comments_;
		std::unordered_map<
            std::string,
            elem>                           elements_;
		std::unordered_map<
            std::type_index,
            std::unique_ptr<
                internal::ErasedInfoBase>>  info_;
		std::unordered_map<
            std::type_index,
            std::unique_ptr<type>>          ios_;
		std::unordered_map<
            const elem*,
            std::string>                    names_;
		std::vector<std::string>            order_;		
	};

	// ---------------------------------------------------------------
	// Property
	// ---------------------------------------------------------------

	inline std::vector<std::reference_wrapper<prop>> elem::properties()
	{
		std::vector<std::reference_wrapper<prop>> info;
		for(auto & [n, p] : properties_)
			info.emplace_back(p);
		return info;
	}

	inline std::vector<std::string> elem::names() const
	{
		std::vector<std::string> info;
		for(auto & [n, p] : properties_)
			info.emplace_back(p.name());
		return info;
	}

	inline void* prop::rawPtr()
	{
		auto & info = *parent_->parent_->info_[tid_].get();
		return info.rawPtr(data_);
	}

	inline std::size_t prop::rawSize()
	{
		auto & info = *parent_->parent_->info_[tid_].get();
		return info.typeSize() * size();
	}

	inline std::size_t prop::size() const
	{
		return parent_->size_;
	}
	inline std::string_view prop::name() const
	{
		return parent_->names_.at(this);
	}
	template<typename T> std::span<T> prop::get()
	{
		if(tid_ != typeid(T))
		{ // late initialization
			if(tid_ == typeid(void))
			{
				tid_ = typeid(T);
				auto & f = parent_->parent_->anyvec_[tid_];
				if(!f) throw std::runtime_error(std::format("Unknown type: \"{}\" (size = {})", typeid(T).name(), sizeof(T)));
				data_ = f(parent_->size_);
			}
			else
				throw std::runtime_error(std::format("Property::get<{}> is incompatible to the stored type \"{}\"", typeid(T).name(), tid_.name()));
		}
		return std::any_cast<std::vector<T> &>(data_);
	}
	template<typename T> void prop::set(std::span<T const> src)
	{
		auto dst = get<T>();
		std::copy_n(src.begin(), src.size(), dst.begin());
	}
	template<typename T> void prop::set(std::vector<T> const & src)
	{
		auto dst = get<T>();
		std::copy_n(src.begin(), src.size(), dst.begin());
	}
	inline std::type_index prop::listType() const
	{
		return tid_;
	}
	inline std::type_index prop::type() const
	{
		return parent_->parent_->info_[tid_]->tid();
	}
	inline bool prop::isList() const
	{
		if(tid_ == typeid(void))
			throw std::runtime_error("Property::isList cannot be used before type initialization");
		return parent_->parent_->info_[tid_]->isList();
	};

	// ---------------------------------------------------------------
	// Element
	// ---------------------------------------------------------------

	inline bool elem::has(std::string_view sv) const
	{
		return properties_.contains(std::string(sv));
	}

	inline prop & elem::operator()(std::string_view name)
	{
		auto [it, inserted] = properties_.try_emplace(std::string(name));
		if(inserted)
		{
			order_.push_back(std::string(name));
			names_[&it->second] = std::string(name);
			it->second.parent_ = this;
		}
		return it->second;
	}

	inline prop & elem::operator()(std::string_view name, std::type_index const & tid)
	{
		if(!parent_->ios_.contains(tid))
			throw std::runtime_error(std::format("No IO defined for type \"{}\"", tid.name()));
		auto [it, inserted] = properties_.try_emplace(std::string(name));
		if(inserted)
		{
			order_.push_back(std::string(name));
			names_[&it->second] = std::string(name);
			it->second.parent_ = this;
			it->second.data_ = parent_->anyvec_[tid](size_);
			it->second.tid_ = tid;
		}
		return it->second;
	}

	inline prop & elem::operator()(std::span<std::string_view const> names)
	{
		for(auto const & n : names)
			if(has(n))
				return operator()(n);
		std::string s;
		for(int i = 0; i < static_cast<int>(names.size()); ++i)
			s += std::string(names[i]) + (i < static_cast<int>(names.size()) - 1 ? " || " : "");
		throw std::runtime_error(std::format("Element does not contain property \"{}\"", s));
	}

	inline std::size_t elem::size() const
	{
		return size_;
	}
	inline std::string_view elem::name() const
	{
		return parent_->names_.at(this);
	}

	inline void elem::del(std::string_view n)
	{
		auto it = properties_.find(std::string(n));
		if(it == properties_.end())
			throw std::runtime_error(std::format("cannot delete something that does not exist element.{}", n));
		names_.erase(&it->second);
		properties_.erase(std::string(n));
		for(std::size_t i = 0; i < order_.size(); i++)
		{
			if(order_[i] == n)
			{
				order_.erase(order_.begin() + i);
				break;
			}
		}
	}

	template<format ff, std::endian ee>
	inline void elem::write(std::ostream & out) const
	{
		std::size_t np = order_.size();
		std::vector<const type *> ios(np); // serializer & deserializer for each property
		std::vector<const void*> ptrs(np); // ptr on the vectors (NOT the data)
		std::vector<std::uint8_t> lsiz(np); // list index type sizes
		for(std::size_t pIdx = 0; pIdx < np; pIdx++)
		{
			auto const & p = properties_.at(order_[pIdx]);
			ios[pIdx] = parent_->ios_[p.tid_].get();
			auto & info = *parent_->info_[p.tid_].get();
			ptrs[pIdx] = info.vecPtr(p.data_);
			lsiz[pIdx] = info.listIndexTypeSize(p.data_);
		}
		if constexpr(ff == format::ascii)
		{
			for(std::size_t i = 0; i < size_; i++)
			{
				for(std::size_t pIdx = 0; pIdx < np; pIdx++)
				{
					ios[pIdx]->ascO(out, ptrs[pIdx], i);
					if(pIdx < np - 1) out << " ";
				}
				if(i < size_ - 1) out << parent_->linesep_;
			}
		}
		else if constexpr(ff == format::binary)
		{
			for(std::size_t i = 0; i < size_; i++)
				for(std::size_t pIdx = 0; pIdx < np; pIdx++)
					ios[pIdx]->binO(out, ptrs[pIdx], i, lsiz[pIdx], ee != std::endian::native);
		}
		else
			throw std::runtime_error("Unknown output format");
	}

	template<format ff, std::endian ee>
	inline void elem::read(std::istream & in)
	{
		std::size_t np = order_.size();
		std::vector<const type *> ios(np); // serializer & deserializer for each property
		std::vector<void*> ptrs(np); // ptr on the vectors (NOT the data)
		std::vector<std::uint8_t> lsiz(np); // list index type sizes
		for(std::size_t pIdx = 0; pIdx < np; pIdx++)
		{
			auto & p = properties_[order_[pIdx]];
			ios[pIdx] = parent_->ios_[p.tid_].get();
			auto & info = *parent_->info_[p.tid_].get();
			ptrs[pIdx] = info.vecPtr(p.data_);
			lsiz[pIdx] = p.listIndexSize_;
		}
		if constexpr(ff == format::ascii)
		{
			for(std::size_t i = 0; i < size_; i++)
				for(std::size_t pIdx = 0; pIdx < np; pIdx++)
					ios[pIdx]->ascI(in, ptrs[pIdx], i);
		}
		else if constexpr(ff == format::binary)
		{
			for(std::size_t i = 0; i < size_; i++)
				for(std::size_t pIdx = 0; pIdx < np; pIdx++)
					ios[pIdx]->binI(in, ptrs[pIdx], i, lsiz[pIdx], ee != std::endian::native);
		}
	}

	// ---------------------------------------------------------------
	// Root
	// ---------------------------------------------------------------

	inline bool root::has(std::string_view en) const
	{
		return elements_.contains(std::string(en));
	}

	inline bool root::has(std::string_view en, std::string_view pn) const
	{
		if(!elements_.contains(std::string(en)))
			return false;
		return elements_.at(std::string(en)).has(pn);
	}

	inline void root::del(std::string_view n)
	{
		auto it = elements_.find(std::string(n));
		if(it == elements_.end())
			throw std::runtime_error(std::format("cannot delete something that does not exist root.{}", n));
		names_.erase(&it->second);
		elements_.erase(std::string(n));
		for(std::size_t i = 0; i < order_.size(); i++)
		{
			if(order_[i] == n)
			{
				order_.erase(order_.begin() + i);
				break;
			}
		}
	}

	inline std::vector<std::reference_wrapper<elem>> root::elements()
	{
		std::vector<std::reference_wrapper<elem>> info;
		for(auto & [n, e] : elements_)
			info.emplace_back(e);
		return info;
	}

	inline std::vector<std::string> root::names() const
	{
		std::vector<std::string> info;
		for(auto & [n, e] : elements_)
			info.emplace_back(e.name());
		return info;
	}

	inline char root::lineSeperator(char newLinesep)
	{
		std::swap(linesep_, newLinesep);
		return newLinesep;
	}

	inline std::vector<std::string> & root::comments()
	{
		return comments_;
	}
	inline elem & root::operator()(std::string_view name, std::size_t size)
	{
		auto [it, inserted] = elements_.try_emplace(std::string(name));
		if(inserted)
		{
			order_.push_back(std::string(name));
			names_[&it->second] = std::string(name);
			it->second.size_ = size;
			it->second.parent_ = this;
		}
		return it->second;
	}
	inline elem & root::operator()(std::string_view name)
	{
		if(!elements_.contains(std::string(name)))
			throw std::runtime_error("cannot access non initialized element");
		// this could be partial initialization without size, but then the size needs to come from "somewhere"...
		return elements_[std::string(name)];
	}
	inline elem const & root::operator()(std::string_view name) const
	{
		if(!elements_.contains(std::string(name)))
			throw std::runtime_error("cannot access non initialized element");
		// this could be partial initialization without size, but then the size needs to come from "somewhere"...
		return elements_.at(std::string(name));
	}

	template<typename T, template<typename, bool> typename CustomIO> inline void root::registerType()
	{
		ios_.insert({typeid(T),std::make_unique<CustomIO<T, false>>()});
		info_.insert({typeid(T),std::make_unique<internal::ErasedInfo<T, false>>()});
		anyvec_.insert({typeid(T), [] (std::size_t s) { return std::vector<T>(s); }});
		ios_.insert({typeid(std::vector<T>),std::make_unique<CustomIO<T, true>>()});
		info_.insert({typeid(std::vector<T>),std::make_unique<internal::ErasedInfo<T, true>>()});
		anyvec_.insert({typeid(std::vector<T>), [] (std::size_t s) { return std::vector<std::vector<T>>(s); }});
	}

	inline std::type_index root::typeidFromStr(std::string_view s, bool isList)
	{
		for(auto const & [tid, ios] : ios_)
			for(auto & sw : ios->names())
				if(sw == s) return isList ? info_[tid]->vecTid() : tid;
		return typeid(void);
	}

	inline void root::read(std::string const & path)
	{
		std::ifstream in(path, std::ios::binary | std::ios::in);
		if(!in.good())
			throw std::runtime_error(std::format("cannot open file in read mode: \"{}\"", path));
		read(in);
	}

	inline void root::read(std::istream & in)
	{
		comments_.clear();
		elements_.clear();
		names_.clear();
		order_.clear();
		std::string line;
		std::size_t lIdx = 0;
		std::uint32_t crlfCounter = 0;
		char lastDecodedChar;
		format fmt = format::ascii;
		std::endian endian = std::endian::native;
		elem * lastElement = nullptr;
		while(true)
		{
			crlfCounter += internal::getline(in, line, lastDecodedChar);
			if(!line.size() || line == str::end_header)
				break;
			if(lIdx == 0)
			{
				if(!line.starts_with(str::ply))
					throw std::runtime_error(std::format("read line {}: wrong magic number in ply file", lIdx + 1));
			}
			else if(lIdx == 1)
			{
				auto a = internal::split(line, str::space);
				if(a.size() < 3 || a[0] != str::format)
					throw std::runtime_error(std::format("read line {}: invalid", lIdx));
				if(a[1] == str::ascii)
					fmt = format::ascii;
				else if(a[1] == str::binary_big_endian)
				{
					fmt = format::binary;
					endian = std::endian::big;
				}
				else if(a[1] == str::binary_little_endian)
				{
					fmt = format::binary;
					endian = std::endian::little;
				}
				else
					throw std::runtime_error(std::format("read line {}: invalid", lIdx));
				if(a[2] != str::version)
					throw std::runtime_error(std::format("read line {}: invalid version. Only 1.0 is supported", lIdx));
			}
			else
			{
				auto a = internal::split(line, str::space);
				switch(line[0])
				{
				case 'c':
				{ // comment ...
					if(a[0] != str::comment)
						throw std::runtime_error(std::format("read line {}: invalid", lIdx));
					comments_.push_back(line.substr(a[0].size() + 1));
				} break;
				case 'e':
				{ // element name size
					if(a[0] != str::elem)
						throw std::runtime_error(std::format("read line {}: invalid", lIdx));
					lastElement = &this->operator()(a[1], std::stoi(a[2]));
				} break;
				case 'p':
				{ // property
					if(a[0] != str::prop)
						throw std::runtime_error(std::format("read line {}: invalid", lIdx));
					if(a[1] == str::list)
					{ // property list type type name
						lastElement->operator()(a[4], typeidFromStr(a[3], true));
						auto listTid = typeidFromStr(a[2], false);
						if(listTid != typeid(std::uint8_t) && listTid != typeid(std::uint16_t) && listTid != typeid(std::uint32_t))
							throw std::runtime_error(std::format("read line {}: invalid property list index type", lIdx));
						lastElement->operator()(a[4]).listIndexSize_ =
							listTid == typeid(std::uint8_t) ? 1 : listTid == typeid(std::uint16_t) ? 2 : 4;
					}
					else
					{ // property type name
						lastElement->operator()(a[2], typeidFromStr(a[1], false));
					}
				} break;
				default:
				{
					// warning might be sufficient, just ignore the line?
					throw std::runtime_error(std::format("read line {}: invalid", lIdx));
				}
				}
			}
			lIdx++;
		}
#ifdef USE_CRLF_LFCR_HEADER_HACK
		auto crlfa = *reinterpret_cast<std::array<std::uint16_t, 2>*>(&crlfCounter);
		// Problem: per definition "end_header\r" is the end of the header, but many implementations also
		// use "end_header\n" or "end_header\r\n" or any amount of \n and \r in any sequence. Why? Noone knows.
		// we expect that there are (avgNSep - 1) seperators after "end_header" left in the stream because one of them was already consumed via getline
		// we expect that all the seperators \n and \r are used without any other characters inbetween
		int avgNSep = static_cast<int>(std::lround((static_cast<float>(crlfa[0]) + static_cast<float>(crlfa[1]) - 1) / lIdx));
		// if avgNSep is no integer -> inconsistent number of linesep per line
		for(int i = 1; i < avgNSep; i++)
		{
			if((lastDecodedChar == str::cr || lastDecodedChar == str::lf) && (in.peek() == str::cr || in.peek() == str::lf))
				in.get(lastDecodedChar);
			else
				throw std::runtime_error("inconsistent line seperators in header");
		}
#endif
		if(fmt == format::ascii)
		{
			std::stringstream ss;
			line.clear();
			while(true)
			{ // ascii can hold a lot of garbage... better sanitize it.
				crlfCounter += internal::getline(in, line, lastDecodedChar);
				if(!line.size()) break;
				ss << line << linesep_;
			}
			for(std::size_t i = 0; i < order_.size(); i++)
			{
				auto & e = elements_[order_[i]];
				e.read<format::ascii>(ss);
			}
		}
		else
		{
			for(std::size_t i = 0; i < order_.size(); i++)
			{
				auto & e = elements_[order_[i]];
				if(endian == std::endian::little)
					e.read<format::binary, std::endian::little>(in);
				else
					e.read<format::binary, std::endian::big>(in);
			}
		}
	}

	template<format ff, std::endian ee>
	inline void root::write(std::string const & path) const
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc | std::ios::out);
		if(!out.good())
			throw std::runtime_error(std::format("Cannot open file in write mode \"{}\"", path));
		write<ff, ee>(out);
	}

	inline std::string root::str() const
	{
		std::ostringstream out;
		write<format::ascii>(out);
		return out.str();
	}

	template<format ff, std::endian ee>
	inline void root::write(std::ostream & out) const
	{
		out << str::ply << linesep_;
		out << str::format << " " << internal::formatName<ff, ee>() << " " << str::version << linesep_;
		for(auto const & c : comments_)
			if(c.find_first_of(str::invalidSymbols) == std::string::npos) // filter all the illegal stuff
				out << str::comment << " " << c << linesep_;
		auto ne = order_.size();
		for(std::size_t i = 0; i < ne; i++)
		{
			auto const & e = elements_.at(order_[i]);
			out << str::elem << " " << e.name() << " " << e.size() << linesep_;
			for(auto const & pname : e.order_)
			{
				auto const & p = e.properties_.at(pname);
				auto const & type = *ios_.at(p.tid_).get();
				auto const & info = *info_.at(p.tid_).get();
				out << str::prop << " ";
				if(info.isList()) out << str::list << " " << internal::listIndexTypeName(info.listIndexTypeSize(p.data_)) << " ";
				out << type.names()[0] << " " << p.name() << linesep_;
			}
		}
		out << str::end_header << linesep_;
		for(std::size_t i = 0; i < ne; i++)
		{
			elements_.at(order_[i]).write<ff, ee>(out);
			if constexpr(ff == format::ascii)
				if(i < ne - 1) out << linesep_;
		}
	}

	namespace internal
	{

		// can be replaced by std::byteswap(x) if everyone _HAS_CXX23
		template<std::integral T> inline constexpr T byteswap(T x)
		{
			if constexpr(sizeof(T) == 1)
				return x;
			else if constexpr(sizeof(T) == 2)
				return static_cast<T>((x << 8) | (x >> 8));
			else if constexpr(sizeof(T) == 4)
				return static_cast<T>((x << 24) | ((x << 8) & 0x00FF0000) | ((x >> 8) & 0x0000FF00) | (x >> 24));
			else if constexpr(sizeof(T) == 8)
				return static_cast<T>((x << 56) | ((x << 40) & 0x00FF000000000000) | ((x << 24) & 0x0000FF0000000000) | ((x << 8) & 0x000000FF00000000) | ((x >> 8) & 0x00000000FF000000) | ((x >> 24) & 0x0000000000FF0000) | ((x >> 40) & 0x000000000000FF00) | (x >> 56));
		}
		template<typename T> inline constexpr void write(std::ostream & out, T x, bool swapEndian)
		{
			if(swapEndian)
			{
				auto b = byteswap(*reinterpret_cast<const internal::uintX<sizeof(T)>*>(&x));
				out.write(reinterpret_cast<const char *>(&b), sizeof(T));
			}
			else
			{
				out.write(reinterpret_cast<const char *>(&x), sizeof(T));
			}
		}
		template<typename T> inline constexpr void read(std::istream & in, T & x, bool swapEndian)
		{
			if(swapEndian)
			{
				uintX<sizeof(T)> b;
				in.read(reinterpret_cast<char *>(&b), sizeof(T));
				b = byteswap(b);
				x = *reinterpret_cast<T *>(&b);
			}
			else
			{
				in.read(reinterpret_cast<char *>(&x), sizeof(T));
			}
		}
		template<typename T, bool isList>
		struct type_x : public type
		{
			void ascI(std::istream & in, void* ptr, std::size_t i) const override
			{
				if constexpr(isList)
				{
					auto & vv = *reinterpret_cast<std::vector<std::vector<T>>*>(ptr);
					if(!vv.size()) return;
					auto & v = vv[i];
					std::int64_t size = 0;
					if(size < 0)
						throw std::runtime_error("Negative size for property list");
					in >> size;
					v.resize(static_cast<std::uint64_t>(size));
					if constexpr(sizeof(T) == 1)
					{
						for(auto & x : v)
						{
							in >> size;
							x = static_cast<T>(size);
						}
					}
					else
						for(auto & x : v) in >> x;
				}
				else if constexpr(!isList)
				{
					auto & v = *reinterpret_cast<std::vector<T>*>(ptr);
					if constexpr(sizeof(T) == 1)
					{
						std::size_t size;
						in >> size;
						v[i] = static_cast<T>(size);
					}
					else
						in >> v[i];
				}
			}
			void ascO(std::ostream & out, const void* ptr, std::size_t i) const override
			{
				if constexpr(isList)
				{
					auto & vv = *reinterpret_cast<const std::vector<std::vector<T>>*>(ptr);
					if(!vv.size()) return;
					auto & v = vv[i];
					out << v.size();
					if constexpr(std::is_same<T, float>::value)
						for(auto & x : v) out << " " << std::format(str::f32fmt, x);
					else if constexpr(std::is_same<T, double>::value)
						for(auto & x : v) out << " " << std::format(str::f64fmt, x);
					else if constexpr(sizeof(T) == 1)
						for(auto & x : v) out << " " << static_cast<int>(x);
					else
						for(auto & x : v) out << " " << x;
				}
				else if constexpr(!isList)
				{
					auto & v = *reinterpret_cast<const std::vector<T>*>(ptr);
					if constexpr(std::is_same<T, float>::value)
						out << std::format(str::f32fmt, v[i]);
					else if constexpr(std::is_same<T, double>::value)
						out << std::format(str::f64fmt, v[i]);
					else if constexpr(sizeof(T) == 1)
						out << static_cast<int>(v[i]);
					else
						out << v[i];
				}
			}
			void binI(std::istream & in, void* ptr, std::size_t i, std::uint8_t listIndexTypeSize, bool swapEndian) const override
			{
				if constexpr(isList)
				{
					auto & vv = *reinterpret_cast<std::vector<std::vector<T>>*>(ptr);
					if(!vv.size()) return;
					auto & v = vv[i];
					switch(listIndexTypeSize)
					{
					case 1:
					{
						uintX<1> x; read(in, x, swapEndian); v.resize(static_cast<std::size_t>(x));
					} break;
					case 2:
					{
						uintX<2> x; read(in, x, swapEndian); v.resize(static_cast<std::size_t>(x));
					} break;
					case 4:
					{
						uintX<4> x; read(in, x, swapEndian); v.resize(static_cast<std::size_t>(x));
					} break;
					default: throw std::runtime_error(std::format("Invalid list index type size: {}", listIndexTypeSize));
					}
					for(auto & x : v) read(in, x, swapEndian);
				}
				else if constexpr(!isList)
				{
					auto & v = *reinterpret_cast<std::vector<T>*>(ptr);
					read(in, v[i], swapEndian);
				}
			}
			void binO(std::ostream & out, const void* ptr, std::size_t i, std::uint8_t listIndexTypeSize, bool swapEndian) const override
			{
				if constexpr(isList)
				{
					auto & vv = *reinterpret_cast<const std::vector<std::vector<T>>*>(ptr);
					if(!vv.size()) return;
					auto & v = vv[i];
					switch(listIndexTypeSize)
					{
					case 1:
					{
						write(out, static_cast<uintX<1>>(v.size()), swapEndian);
					} break;
					case 2:
					{
						write(out, static_cast<uintX<2>>(v.size()), swapEndian);
					} break;
					case 4:
					{
						write(out, static_cast<uintX<4>>(v.size()), swapEndian);
					} break;
					default: throw std::runtime_error(std::format("Invalid list index type size: {}", listIndexTypeSize));
					}
					for(auto & x : v) write(out, x, swapEndian);
				}
				else if constexpr(!isList)
				{
					auto & v = *reinterpret_cast<const std::vector<T>*>(ptr);
					write(out, v[i], swapEndian);
				}
			}
			std::vector<std::string_view> names() const override
			{
				if      constexpr(std::is_same<T, std::int8_t  >::value) return {str::t_char, str::t_int8};
				else if constexpr(std::is_same<T, std::uint8_t >::value) return {str::t_uchar, str::t_uint8};
				else if constexpr(std::is_same<T, std::int16_t >::value) return {str::t_short, str::t_int16};
				else if constexpr(std::is_same<T, std::uint16_t>::value) return {str::t_ushort, str::t_uint16};
				else if constexpr(std::is_same<T, std::int32_t >::value) return {str::t_int, str::t_int32};
				else if constexpr(std::is_same<T, std::uint32_t>::value) return {str::t_uint, str::t_uint32};
				else if constexpr(std::is_same<T, float        >::value) return {str::t_float, str::t_float32};
				else if constexpr(std::is_same<T, double       >::value) return {str::t_double, str::t_float64};
				else return {};
			}
		};
	}

	inline root::root()
	{ // load default (ply 1.0) datatypes on construction.
		registerType<float, internal::type_x>();
		registerType<double, internal::type_x>();
		registerType<std::int8_t, internal::type_x>();
		registerType<std::uint8_t, internal::type_x>();
		registerType<std::int16_t, internal::type_x>();
		registerType<std::uint16_t, internal::type_x>();
		registerType<std::int32_t, internal::type_x>();
		registerType<std::uint32_t, internal::type_x>();
	}

}

