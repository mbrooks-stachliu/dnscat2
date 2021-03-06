##
# dnscat2_test.rb
# Created March, 2013
# By Ron Bowes
#
# See: LICENSE.txt
#
# Self tests for dnscat2_server.rb - implements a fake "client" that's
# basically just a class.
##

require 'packet'

class Test
  MY_DATA = "this is MY_DATA"
  MY_DATA2 = "this is MY_DATA2"
  MY_DATA3 = "this is MY_DATA3"
  THEIR_DATA = "This is THEIR_DATA"

  THEIR_ISN = 0x4444
  MY_ISN    = 0x3333

  SESSION_ID = 0x1234

  def initialize()
    @data = []

    my_seq     = MY_ISN
    their_seq  = THEIR_ISN
    packet_id  = rand(0xFFFF)

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq, their_seq, MY_DATA),
      :recv => Packet.create_fin(packet_id, SESSION_ID),
      :name => "Sending an unexpected MSG (should respond with a FIN)",
    }

    @data << {
    #                            PACKET_ID, ID          SEQ        ACK                      DATA
      :send => Packet.create_fin(packet_id, SESSION_ID),
      :recv => nil,
      :name => "Sending an unexpected FIN (should respond with a FIN)",
    }

    @data << {
      :send => Packet.create_syn(packet_id, SESSION_ID, my_seq),
      :recv => Packet.create_syn(packet_id, SESSION_ID, their_seq),
      :name => "Initial SYN (SEQ 0x%04x => 0x%04x)" % [my_seq, their_seq],
    }

    @data << {
      :send => Packet.create_syn(packet_id, SESSION_ID, 0x3333, 0), # Duplicate SYN
      :recv => nil,
      :name => "Duplicate SYN (should be ignored)",
    }

    @data << {
      :send => Packet.create_syn(packet_id, 0x4321, 0x5555),
      :recv => Packet.create_syn(packet_id, 0x4321, 0x4444),
      :name => "Initial SYN, session 0x4321 (SEQ 0x5555 => 0x4444) (should create new session)",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq,               MY_DATA),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq + MY_DATA.length, THEIR_DATA),
      :name => "Sending some initial data",
    }
    my_seq += MY_DATA.length # Update my seq

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq+1,   0,     "This is more data with a bad SEQ"),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq, THEIR_DATA),
      :name => "Sending data with a bad SEQ (too low), this should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq - 100,   0,   "This is more data with a bad SEQ"),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq, THEIR_DATA),
      :name => "Sending data with a bad SEQ (way too low), this should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq+100, 0,   "This is more data with a bad SEQ"),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq, THEIR_DATA),
      :name => "Sending data with a bad SEQ (too high), this should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq,                 MY_DATA2),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq + MY_DATA2.length,  THEIR_DATA),
      :name => "Sending another valid packet, but with a bad ACK, causing the server to repeat the last message",
    }
    my_seq += MY_DATA2.length

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq ^ 0xFFFF, ""),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq,             THEIR_DATA),
      :name => "Sending a packet with a very bad ACK, which should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq - 1,      ""),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq,             THEIR_DATA),
      :name => "Sending a packet with a slightly bad ACK (one too low), which should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq + THEIR_DATA.length + 1, ""),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq,                            THEIR_DATA),
      :name => "Sending a packet with a slightly bad ACK (one too high), which should trigger a re-send",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,        their_seq + 1, ""),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq + 1, my_seq,        THEIR_DATA[1..-1]),
      :name => "ACKing the first byte of their data, which should cause them to send the second byte and onwards",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,        their_seq + 1, ""),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq + 1, my_seq,        THEIR_DATA[1..-1]),
      :name => "ACKing just the first byte again",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,        their_seq + 1,             MY_DATA3),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq + 1, my_seq + MY_DATA3.length,  THEIR_DATA[1..-1]),
      :name => "Still ACKing the first byte, but sending some more of our own data",
    }
    my_seq += MY_DATA3.length

    their_seq += THEIR_DATA.length
    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq, ''),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq,    ''),
      :name => "ACKing their data properly, they should respond with nothing",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq, ''),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq,    ''),
      :name => "Sending a blank MSG packet, expecting to receive a black MSG packet",
    }

    @data << {
      :send => Packet.create_syn(packet_id, SESSION_ID, my_seq),
      :recv => nil,
      :name => "Attempting to send a SYN before the FIN - should be ignored",
    }

    @data << {
      :send => Packet.create_fin(packet_id, SESSION_ID),
      :recv => Packet.create_fin(packet_id, SESSION_ID),
      :name => "Sending a FIN, should receive a FIN",
    }

    # Re-set the ISNs
    my_seq     = MY_ISN - 1000
    their_seq  = THEIR_ISN
    @data << {
      :send => Packet.create_syn(packet_id, SESSION_ID, my_seq),
      :recv => Packet.create_syn(packet_id, SESSION_ID, their_seq),
      :name => "Attempting re-use the old session id - this should work flawlessly",
    }

    @data << {
      :send => Packet.create_msg(packet_id, SESSION_ID, my_seq,    their_seq,               MY_DATA),
      :recv => Packet.create_msg(packet_id, SESSION_ID, their_seq, my_seq + MY_DATA.length, ""),
      :name => "Sending initial data in the new session",
    }
    my_seq += MY_DATA.length # Update my seq

    # Re-set the ISNs
    my_seq     = MY_ISN - 1000
    their_seq  = THEIR_ISN
    @data << {
      :send => Packet.create_syn(packet_id, 0x4411, my_seq),
      :recv => Packet.create_syn(packet_id, 0x4411, their_seq),
      :name => "Attempting re-use the old session id - this should work flawlessly",
    }

    @data << {
      :send => Packet.create_msg(packet_id, 0x4411, my_seq,    their_seq,               MY_DATA),
      :recv => Packet.create_msg(packet_id, 0x4411, their_seq, my_seq + MY_DATA.length, ""),
      :name => "Sending initial data in the new session",
    }

    # Close both sessions
    @data << {
      :send => Packet.create_fin(packet_id, SESSION_ID),
      :recv => Packet.create_fin(packet_id, SESSION_ID),
      :name => "Sending a FIN, should receive a FIN",
    }

    @data << {
      :send => Packet.create_fin(packet_id, 0x4411),
      :recv => Packet.create_fin(packet_id, 0x4411),
      :name => "Sending a FIN, should receive a FIN",
    }

    @data << {
      :send => Packet.create_fin(packet_id, SESSION_ID),
      :recv => nil,
      :name => "Sending a FIN for a session that's already closed, it should ignore it",
    }

    return
  end

  def recv()
    loop do
      if(@data.length == 0)
        raise(IOError, "Connection closed")
      end

      out = @data.shift
      response = yield(out[:send])

      if(response != out[:recv])
        @@failure += 1
        puts(out[:name])
        puts(" >> Expected: #{out[:recv].nil? ? "<no response> " : Packet.parse(out[:recv])}")
        puts(" >> Received: #{Packet.parse(response)}")
      else
        @@success += 1
        puts("SUCCESS: #{out[:name]}")
      end
    end
  end

  def send(data)
    # Just ignore the data being sent
  end

  def close()
    # Do nothing
  end

  def Test.do_test()
    begin
      @@success = 0
      @@failure = 0

      Session.debug_set_isn(0x4444)
      session = Session.find(SESSION_ID)
      session.queue_outgoing(THEIR_DATA)
      Dnscat2.go(Test.new)
    rescue IOError => e
      puts("IOError was thrown (as expected): #{e}")
      puts("Tests passed: #{@@success} / #{@@success + @@failure}")
    end

    exit
  end
end
