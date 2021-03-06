/*
    Copyright (c) 2011-2013 Andrey Sibiryov <me@kobology.ru>
    Copyright (c) 2011-2013 Other contributors as noted in the AUTHORS file.

    This file is part of Cocaine.

    Cocaine is free software; you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    Cocaine is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COCAINE_IO_PROTOCOL_TRAVERSE_HPP
#define COCAINE_IO_PROTOCOL_TRAVERSE_HPP

#include "cocaine/rpc/protocol.hpp"
#include "cocaine/rpc/tags.hpp"
#include "cocaine/rpc/tree.hpp"

#include <boost/mpl/end.hpp>
#include <boost/mpl/deref.hpp>

namespace cocaine { namespace io {

template<class Tag>
auto
traverse() -> boost::optional<dispatch_tree_t>;

namespace aux {

template<class It, class End>
struct traverse_impl {
    static inline
    void
    invoke(dispatch_tree_t& object) {
        typedef typename boost::mpl::deref<It>::type event_type;
        typedef event_traits<event_type> traits_type;

        object[traits_type::id] = std::make_tuple(
            event_type::alias(),
            traverse<typename traits_type::transition_type>()
        );

        traverse_impl<typename boost::mpl::next<It>::type, End>::invoke(object);
    }
};

template<class End>
struct traverse_impl<End, End> {
    static inline
    void
    invoke(dispatch_tree_t& /* object */) {
        // Empty.
    }
};

} // namespace aux

template<class Tag>
inline
auto
traverse() -> boost::optional<dispatch_tree_t> {
    dispatch_tree_t result;

    aux::traverse_impl<
        typename boost::mpl::begin<typename protocol<Tag>::type>::type,
        typename boost::mpl::end<typename protocol<Tag>::type>::type
    >::invoke(result);

    return result;
}

template<>
inline
auto
traverse<recursive_tag>() -> boost::optional<dispatch_tree_t> {
    return boost::none_t();
}

}} // namespace cocaine::io

#endif
