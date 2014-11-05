require 'mkmf'

if !have_header("sys/epoll.h")
  puts "[31m*** complie error: gem 'io-epoll' must be installed <sys/epoll.h>. ***[m"
  puts "[31m*** you can require 'io/epoll'. But, you can not use IO::Epoll APIs. ***[m"
end
create_makefile('io/epoll/epoll')
