#ifndef EPOLL_HELPER_HPP_INCLUDED
#define EPOLL_HELPER_HPP_INCLUDED
inline bool is_eagain(int err_code) {
    return err_code==EAGAIN||err_code==EWOULDBLOCK;
}
#endif
