//-------------------- Licensed after GNU GPL v3 -----------------------------------------
/*It`s my copy for std::vector
  made just for fun*/
//-------------------- clang++ --std=c++20 -O2 -DEXAMPLE super_vec.cc -o vec -------------

#include <iostream>
#include <initializer_list>
#include <utility>
#include <algorithm>
#include <memory>
#include <iterator>

namespace Containers {

    template<typename T>
    class VectorBuffer {
    protected:
        T* data_;
        size_t size_;
        size_t capacity_;

        VectorBuffer() : data_(nullptr), size_(0), capacity_(0) {}

        VectorBuffer(size_t num) : data_(static_cast<T*>
                    (::operator new(num * sizeof(T)))), size_(0), capacity_(num) {}

        VectorBuffer(VectorBuffer&& rhs) noexcept {
            std::swap(size_, rhs.size_);
            std::swap(capacity_, rhs.capacity_);
            std::swap(data_, rhs.data_);
        }

        VectorBuffer& operator=(VectorBuffer&& rhs) noexcept {
            if (this != &rhs) {
                std::swap(size_, rhs.size_);
                std::swap(capacity_, rhs.capacity_);
                std::swap(data_, rhs.data_);
            }
            return *this;
        }

        VectorBuffer(const VectorBuffer&) = delete;
        VectorBuffer& operator=(const VectorBuffer&) = delete;

        ~VectorBuffer() {
            std::destroy(data_, data_ + size_);
            ::operator delete(data_);
        }
    };

    template<typename T>
    class Vector final : private VectorBuffer<T> {
        static_assert(std::is_nothrow_move_constructible<T>::value || 
                      std::is_nothrow_copy_constructible<T>::value, 
                      "Type is invalid");
    private:
        using VectorBuffer<T>::size_;
        using VectorBuffer<T>::capacity_;
        using VectorBuffer<T>::data_;

    public:
        Vector() = default;
        Vector(Vector&&) = default;
        Vector& operator=(Vector&&) = default;

    private:
        class nothing final {};
        using CopyArg = std::conditional_t<std::is_copy_constructible_v<T>, const Vector&, int>;

        template<typename Args>
        using TCopyEnable = std::void_t<decltype(Args(std::declval<Args&>()))>;

        template<typename Args>
        using TDefCtor = std::void_t<decltype(Args())>;

    public:
        Vector(CopyArg lhs) : VectorBuffer<T>(lhs.size_) {
            for (; size_ < lhs.size_; ++size_)
                new (data_ + size_) T{lhs.data_[size_]};
        }

        Vector& operator=(CopyArg lhs) {
            Vector copy(lhs);
            return *this = std::move(copy);
        }

        template<typename Args = T, typename = TCopyEnable<Args>>
        Vector(std::initializer_list<Args> args) : VectorBuffer<T>(args.size()) {
            static_assert(std::is_same_v<T, Args>, "Arguments of init list must be the same type");
            for (auto it = args.begin(), end = args.end(); it != end; ++it, ++size_)
                new (data_ + size_) T{*it};
        }

        template<typename Args = T, typename = TCopyEnable<Args>>
        Vector(size_t size, const T& val) : VectorBuffer<T>(size) {
            static_assert(std::is_same_v<T, Args>, "Arguments of init list must be the same type");
            for (; size_ < size; ++size_)
                new (data_ + size_) T(val);
        }

        template<typename Args = T, typename  = TDefCtor<Args>>
        Vector(size_t size) : VectorBuffer<T>(size) {
            static_assert(std::is_same_v<T, Args>, "Arguments of init list must be the same type");
            for (; size_ < size; ++size_)
                new (data_ + size_) T{};
        }

        ~Vector() = default;

        static void swap(Vector& lhs, Vector& rhs) {
            std::swap(lhs.data_, rhs.data_);
            std::swap(lhs.size_, rhs.size_);
            std::swap(lhs.capacity_, rhs.capacity_);
        }

    private:
        template<typename Arg>
        using ChooseCopy = std::enable_if_t<std::is_nothrow_copy_constructible_v<Arg>, int>;

        template<typename Arg>
        using ChooseMove = std::enable_if_t<std::is_nothrow_move_constructible_v<Arg>, int>;

        template<typename Arg = T, ChooseCopy<Arg> = 0>
        void moveOrCopyT(T* dest, T& src) {
            new (dest) T(src);
        }

        template<typename Arg = T, ChooseMove<Arg> = 0>
        void moveOrCopyT(T* dest, T& src) {
            new (dest) T(std::move(src));
        }

        Vector(size_t size, nothing) : VectorBuffer<T>(size) {}

        void realloc(size_t newAlloc) {
            size_t newSize = std::min(newAlloc, size_);
            Vector newVector(newAlloc, nothing{});

            for (; newVector.size_ < newSize; ++newVector.size_)
                moveOrCopyT(newVector.data_ + newVector.size_, data_[newVector.size_]);

            *this = std::move(newVector);
        }

    public:

        void clear() {
            std::destroy(data_, data_ + size_);
            size_ = 0;
        }

        void destroy() {
            clear();
            ::operator delete(data_);
            data_ = nullptr;
            size_ = 0;
            capacity_ = 0;
        }

        template<typename Arg>
        void push(Arg&& val) {
            if (size_ == capacity_)
                realloc(2 * size_ + 1);
            new (data_ + size_) T(std::forward<Arg>(val));
            ++size_;
        }

        void pop() {
            if (size_ == 0)
                throw std::invalid_argument("Impossible to pop from empty vector");
            data_[size_ - 1].~T();
            --size_;
        }

        T& back() noexcept { return data_[size_ - 1]; }
        const T& back() const noexcept { return data_[size_ - 1]; }

        T& front() noexcept { return *data_; }
        const T& front() const noexcept { return *data_; }
    
    //----------Iterators-------------
        class iterator final {
        private:
            friend Vector;
            T* ptr_;

            iterator(T* ptr) noexcept : ptr_(ptr) {}

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = T*;
            using reference = T&;
            using iterator_category = std::random_access_iterator_tag;

            iterator() = default;
            iterator(const iterator&) = default;

            iterator& operator=(const iterator&) = default;

            T& operator*() const noexcept { return *ptr_; }
            T* operator->() const noexcept { return ptr_; }

            iterator& operator++() noexcept {
                ++ptr_;
                return *this;
            }

            iterator operator++(int) noexcept {
                iterator copy{*this};
                ++ptr_;
                return copy;
            }

            iterator& operator--() noexcept {
                --ptr_;
                return *this;
            }

            iterator operator--(int) noexcept {
                iterator copy{*this};
                --ptr_;
                return copy;
            }

            iterator& operator+=(difference_type offset) noexcept {
                ptr_ += offset;
                return *this;
            }

            iterator operator+(difference_type offset) const noexcept {
                iterator copy{*this};
                copy += offset;
                return copy;
            }

            iterator& operator-=(difference_type offset) noexcept {
                ptr_ -= offset;
                return *this;
            }

            iterator operator-(difference_type offset) const noexcept {
                iterator copy{*this};
                copy -= offset;
                return copy;
            }

            difference_type operator-(const iterator& other) const noexcept {
                return ptr_ - other.ptr_;
            }

            T& operator[](difference_type offset) const noexcept { return ptr_[offset]; }

            bool operator==(const iterator& second) const noexcept = default;
            bool operator!=(const iterator& second) const noexcept = default;
            bool operator<(const iterator& other) const noexcept { return ptr_ < other.ptr_; }
            bool operator>(const iterator& other) const noexcept { return ptr_ > other.ptr_; }
            bool operator<=(const iterator& other) const noexcept { return ptr_ <= other.ptr_; }
            bool operator>=(const iterator& other) const noexcept { return ptr_ >= other.ptr_; }
        };

        class const_iterator {
        friend Vector;
        const T* ptr_;

        const_iterator(const T* ptr) noexcept : ptr_(ptr) {}

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = const T*;
            using reference = const T&;
            using iterator_category = std::random_access_iterator_tag;

            const_iterator() = default;
            const_iterator(const const_iterator&) = default;
            const_iterator(const iterator& it) noexcept : ptr_(it.ptr_) {}
            const_iterator& operator=(const const_iterator&) = default;
            const_iterator& operator=(const iterator& it) noexcept {
                ptr_ = it.ptr_;
                return *this;
            }

            const T& operator*() const noexcept { return *ptr_; }
            const T* operator->() const noexcept { return ptr_; }

            const_iterator& operator++() noexcept {
                ++ptr_;
                return *this;
            }
            const_iterator operator++(int) noexcept {
                const_iterator copy{*this};
                ++ptr_;
                return copy;
            }
            const_iterator& operator--() noexcept {
                --ptr_;
                return *this;
            }
            const_iterator operator--(int) noexcept {
                const_iterator copy{*this};
                --ptr_;
                return copy;
            }

            const_iterator& operator+=(difference_type offset) noexcept {
                ptr_ += offset;
                return *this;
            }
            const_iterator operator+(difference_type offset) const noexcept {
                const_iterator copy{*this};
                copy += offset;
                return copy;
            }
            const_iterator& operator-=(difference_type offset) noexcept {
                ptr_ -= offset;
                return *this;
            }
            const_iterator operator-(difference_type offset) const noexcept {
                const_iterator copy{*this};
                copy -= offset;
                return copy;
            }

            difference_type operator-(const const_iterator& other) const noexcept {
                return ptr_ - other.ptr_;
            }

            const T& operator[](difference_type offset) const noexcept { return ptr_[offset]; }

            bool operator==(const const_iterator& second) const noexcept = default;
            bool operator!=(const const_iterator& second) const noexcept = default;
            bool operator<(const const_iterator& other) const noexcept { return ptr_ < other.ptr_; }
            bool operator>(const const_iterator& other) const noexcept { return ptr_ > other.ptr_; }
            bool operator<=(const const_iterator& other) const noexcept { return ptr_ <= other.ptr_; }
            bool operator>=(const const_iterator& other) const noexcept { return ptr_ >= other.ptr_; }
        };

        class reverse_iterator final {
        private:
            friend Vector;
            T* ptr_;

            reverse_iterator(T* ptr) noexcept : ptr_(ptr) {}

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = T*;
            using reference = T&;
            using iterator_category = std::random_access_iterator_tag;

            reverse_iterator() = default;
            reverse_iterator(const reverse_iterator&) = default;

            reverse_iterator& operator=(const reverse_iterator&) = default;

            T& operator*() const noexcept { return *ptr_; }
            T* operator->() const noexcept { return ptr_; }

            reverse_iterator& operator++() noexcept {
                --ptr_;
                return *this;
            }

            reverse_iterator operator++(int) noexcept {
                reverse_iterator copy{*this};
                --ptr_;
                return copy;
            }

            reverse_iterator& operator--() noexcept {
                ++ptr_;
                return *this;
            }

            reverse_iterator operator--(int) noexcept {
                reverse_iterator copy{*this};
                ++ptr_;
                return copy;
            }

            reverse_iterator& operator+=(difference_type offset) noexcept {
                ptr_ -= offset;
                return *this;
            }

            reverse_iterator operator+(difference_type offset) const noexcept {
                reverse_iterator copy{*this};
                copy -= offset;
                return copy;
            }

            reverse_iterator& operator-=(difference_type offset) noexcept {
                ptr_ += offset;
                return *this;
            }

            reverse_iterator operator-(difference_type offset) const noexcept {
                reverse_iterator copy{*this};
                copy += offset;
                return copy;
            }

            difference_type operator-(const reverse_iterator& other) const noexcept {
                return other.ptr_ - ptr_;
            }

            T& operator[](difference_type offset) const noexcept { return ptr_[-offset]; }

            bool operator==(const reverse_iterator& second) const noexcept = default;
            bool operator!=(const reverse_iterator& second) const noexcept = default;
            bool operator<(const reverse_iterator& other) const noexcept { return ptr_ > other.ptr_; }
            bool operator>(const reverse_iterator& other) const noexcept { return ptr_ < other.ptr_; }
            bool operator<=(const reverse_iterator& other) const noexcept { return ptr_ >= other.ptr_; }
            bool operator>=(const reverse_iterator& other) const noexcept { return ptr_ <= other.ptr_; }
        };

        class const_reverse_iterator {
            friend Vector;
            const T* ptr_;

            const_reverse_iterator(const T* ptr) noexcept : ptr_(ptr) {}

        public:
            using difference_type = std::ptrdiff_t;
            using value_type = T;
            using pointer = const T*;
            using reference = const T&;
            using iterator_category = std::random_access_iterator_tag;

            const_reverse_iterator() = default;
            const_reverse_iterator(const const_reverse_iterator&) = default;
            const_reverse_iterator(const reverse_iterator& it) noexcept : ptr_(it.ptr_) {}
            const_reverse_iterator& operator=(const const_reverse_iterator&) = default;
            const_reverse_iterator& operator=(const reverse_iterator& it) noexcept {
                ptr_ = it.ptr_;
                return *this;
            }

            const T& operator*() const noexcept { return *ptr_; }
            const T* operator->() const noexcept { return ptr_; }

            const_reverse_iterator& operator++() noexcept {
                --ptr_;
                return *this;
            }

            const_reverse_iterator operator++(int) noexcept {
                const_reverse_iterator copy{*this};
                --ptr_;
                return copy;
            }

            const_reverse_iterator& operator--() noexcept {
                ++ptr_;
                return *this;
            }

            const_reverse_iterator operator--(int) noexcept {
                const_reverse_iterator copy{*this};
                ++ptr_;
                return copy;
            }

            const_reverse_iterator& operator+=(difference_type offset) noexcept {
                ptr_ -= offset;
                return *this;
            }

            const_reverse_iterator operator+(difference_type offset) const noexcept {
                const_reverse_iterator copy{*this};
                copy -= offset;
                return copy;
            }

            const_reverse_iterator& operator-=(difference_type offset) noexcept {
                ptr_ += offset;
                return *this;
            }

            const_reverse_iterator operator-(difference_type offset) const noexcept {
                const_reverse_iterator copy{*this};
                copy += offset;
                return copy;
            }

            difference_type operator-(const const_reverse_iterator& other) const noexcept {
                return other.ptr_ - ptr_;
            }

            const T& operator[](difference_type offset) const noexcept { return ptr_[-offset]; }

            bool operator==(const const_reverse_iterator& second) const noexcept = default;
            bool operator!=(const const_reverse_iterator& second) const noexcept = default;
            bool operator<(const const_reverse_iterator& other) const noexcept { return ptr_ > other.ptr_; }
            bool operator>(const const_reverse_iterator& other) const noexcept { return ptr_ < other.ptr_; }
            bool operator<=(const const_reverse_iterator& other) const noexcept { return ptr_ >= other.ptr_; }
            bool operator>=(const const_reverse_iterator& other) const noexcept { return ptr_ <= other.ptr_; }
        };
        //---------------------

        iterator begin() noexcept { return iterator(data_); }
        iterator end() noexcept { return iterator(data_ + size_); }
        const_iterator begin() const noexcept { return const_iterator(data_); }
        const_iterator end() const noexcept { return const_iterator(data_ + size_); }
        const_iterator cbegin() const noexcept { return const_iterator(data_); }
        const_iterator cend() const noexcept { return const_iterator(data_ + size_); }

        reverse_iterator rbegin() noexcept { return reverse_iterator(data_ + size_ - 1); }
        reverse_iterator rend() noexcept { return reverse_iterator(data_ - 1); }
        const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(data_ + size_ - 1); }
        const_reverse_iterator rend() const noexcept { return const_reverse_iterator(data_ - 1); }
        const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(data_ + size_ - 1); }
        const_reverse_iterator crend() const noexcept { return const_reverse_iterator(data_ - 1); }


        T& operator[](size_t id) noexcept { return data_[id]; }
        const T& operator[](size_t id) const noexcept { return data_[id]; }

        size_t allocated() const noexcept { return capacity_; }
        size_t size() const noexcept { return size_; }
    };
}

int main() {
    #ifdef EXAMPLE
    Containers::Vector<int> v{1, 2, 3, 4, 5};

    for(auto it = v.begin(); it != v.end(); ++it)
        std::cout << *it << ' ';
    std::cout << std::endl;

    for(auto&& elem: v )
        std::cout << elem << " " ;
    std::cout << std::endl;

    for(auto it = v.rbegin(); it != v.rend(); ++it)
        std::cout << *it << ' ';
    std::cout << std::endl;

    std::cout << std::distance(v.begin(), v.end()) << 
        " from std::distance euqals to " << v.size()<< " by ::size() " ;

    v.clear();
    #endif
    return 0;
}
