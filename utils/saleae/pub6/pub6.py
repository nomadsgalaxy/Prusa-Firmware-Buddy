from saleae.analyzers import HighLevelAnalyzer, AnalyzerFrame


def port_id_to_name(port_id):
    if port_id == 8184:
        return 'uavcan.diagnostic.Record.1'
    if port_id == 8166:
        return 'uavcan.pnp.NodeIDAllocationData.1'
    if port_id == 8165:
        return 'uavcan.pnp.NodeIDAllocationData.2'
    if port_id == 7509:
        return 'uavcan.node.Heartbeat.1'
    if port_id == 435:
        return 'uavcan.node.ExecuteCommand.1'
    if port_id == 430:
        return 'uavcan.node.GetInfo.1'
    if port_id == 408:
        return 'uavcan.file.Read.1'
    return str(port_id)


def decode_canid(canid):
    if canid & (1 << 23):
        return None

    prio = (canid >> 26) & 7
    src = canid & 127
    srv_not_msg = canid & (1 << 25)
    if srv_not_msg:
        port = (canid >> 14) & (2**9 - 1)
        req_not_res = canid & (1 << 24)
        dst = (canid >> 7) & 127
        data = {
            'kind': 'req' if req_not_res else 'res',
            'prio': str(prio),
            'port': port_id_to_name(port),
            'src': str(src),
            'dst': str(dst),
        }

    else:
        if canid & (1 << 7):
            return None

        port = (canid >> 8) & (2**13 - 1)
        anon = canid & (1 << 24)
        data = {
            'kind': 'msg',
            'prio': str(prio),
            'port': port_id_to_name(port),
        }
        if not anon:
            data['src'] = str(src)

    return data


class Analyzer2(object):
    """Second stage analyzer for decoding internal CAN frames to PUB6 messages"""

    def __init__(self):
        self.transfers = {}
        self.last_time = None

    def decode(self, canid, value, start_time, end_time):
        # slice-off tail byte
        tail = value[-1]
        payload = value[:-1]

        # unpack tail byte
        start_of_transfer = bool(tail & 0x80)
        end_of_transfer = bool(tail & 0x40)
        transfer_id = int(tail & 0x1f)

        key = canid, transfer_id
        if start_of_transfer:
            self.transfers[key] = start_time, payload
        else:
            old_start_time, old_payload = self.transfers[key]
            self.transfers[key] = old_start_time, old_payload + payload

        if end_of_transfer:
            old_start_time, old_payload = self.transfers.pop(key)
            if not start_of_transfer:
                # slice-off crc for multi-frame transfers
                old_payload = old_payload[:-2]
            return self.emit(canid, transfer_id, old_payload, old_start_time,
                             end_time)

        return None

    def emit(self, canid, transfer_id, payload, start_time, end_time):
        data = decode_canid(canid)
        if not data:
            return None

        kind = data.pop('kind')
        data['tid'] = str(transfer_id)
        data['data'] = payload

        # Saleae can't handle time going backwards, so we crop the time
        # and just suffer incorrect start_time for interrupted multi-frame messages.
        if self.last_time is not None and start_time < self.last_time:
            start_time = self.last_time
        self.last_time = end_time
        return AnalyzerFrame(kind, start_time, end_time, data)


class Analyzer(HighLevelAnalyzer):
    """First-stage analyzer for decoding Molinaro CANFD frames to internal CAN frames"""

    def __init__(self):
        self.canid = None
        self.start_time = None
        self.value = bytes()
        self.analyzer2 = Analyzer2()

    def decode(self, frame: AnalyzerFrame):
        if frame.type == 'Ext Idf':
            self.canid = int.from_bytes(frame.data['Value'], byteorder='big')
            self.start_time = frame.start_time
            self.value = bytes()
        elif frame.type.startswith('D'):
            self.value += frame.data['Value']
        elif frame.type == 'EOF':
            return self.analyzer2.decode(self.canid, self.value,
                                         self.start_time, frame.end_time)
