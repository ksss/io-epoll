require 'epoll/epoll'

class Epoll
  attr_accessor :evlist

  include Epoll::Constants

  class << self
    alias create open
  end

  def size
    @evlist.size
  end
  alias length size

  def add(io, events)
    ctl CTL_ADD, io, events
    @evlist << io
    self
  end

  def mod(io, events)
    ctl CTL_MOD, io, events
  end

  def del(io)
    ctl CTL_DEL, io
    @evlist.delete(io)
    self
  end
end
