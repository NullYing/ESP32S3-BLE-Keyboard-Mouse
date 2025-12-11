#!/usr/bin/env python3
"""
测试脚本：对比 C++ 和 C 版本的 HID 报告描述符解析结果
"""
import struct

# HID 描述符数据（从终端输出复制）
descriptor_hex = """
05 01 09 02 A1 01 85 02 09 01 A1 00 05 09 19 01 
29 10 15 00 25 01 95 10 75 01 81 02 05 01 16 01 
80 26 FF 7F 75 10 95 02 09 30 09 31 81 06 15 81 
25 7F 75 08 95 01 09 38 81 06 05 0C 0A 38 02 95 
01 81 06 C0 C0 05 0C 09 01 A1 01 85 03 75 10 95 
02 15 01 26 FF 02 19 01 2A FF 02 81 00 C0 05 01 
09 80 A1 01 85 04 75 02 95 01 15 01 25 03 09 82 
09 81 09 83 81 60 75 06 81 03 C0 06 BC FF 09 88 
A1 01 85 08 19 01 29 FF 15 01 26 FF 00 75 08 95 
01 81 00 C0
"""

def parse_hex(hex_str):
    """将十六进制字符串转换为字节数组"""
    return bytes.fromhex(hex_str.replace('\n', ' ').strip())

def parse_hid_item(descriptor, offset):
    """解析单个 HID 项"""
    if offset >= len(descriptor):
        return None, offset
    
    b = descriptor[offset]
    offset += 1
    
    # Long item
    if b == 0xFE:
        if offset >= len(descriptor):
            return None, offset
        data_size = descriptor[offset]
        offset += 1
        long_data_size = descriptor[offset]
        offset += 1
        offset += long_data_size
        return {'type': 'LONG', 'data': descriptor[offset-long_data_size:offset]}, offset
    
    # Short item
    item_size = b & 0x03
    if item_size == 3:
        item_size = 4
    
    item_type = (b >> 2) & 0x03
    item_tag = (b >> 4) & 0x0F
    
    if offset + item_size > len(descriptor):
        return None, offset
    
    data = descriptor[offset:offset+item_size]
    offset += item_size
    
    return {
        'byte': b,
        'type': item_type,
        'tag': item_tag,
        'size': item_size,
        'data': data
    }, offset

def decode_uint(data, signed=False):
    """解码无符号/有符号整数"""
    if len(data) == 0:
        return 0
    if len(data) == 1:
        return struct.unpack('b' if signed else 'B', data)[0]
    if len(data) == 2:
        return struct.unpack('>h' if signed else '>H', data)[0]
    if len(data) == 4:
        return struct.unpack('>i' if signed else '>I', data)[0]
    return 0

def decode_usage(data):
    """解码 Usage 值（可能是 1, 2 或 4 字节）"""
    if len(data) == 1:
        return (0, data[0])
    elif len(data) == 2:
        return (0, struct.unpack('>H', data)[0])
    elif len(data) == 4:
        usage_page = struct.unpack('>H', data[2:4])[0]
        usage = struct.unpack('>H', data[0:2])[0]
        return (usage_page, usage)
    return (0, 0)

# HID 项类型
ITEM_TYPE_MAIN = 0
ITEM_TYPE_GLOBAL = 1
ITEM_TYPE_LOCAL = 2

# Main items
MAIN_INPUT = 0x80
MAIN_OUTPUT = 0x90
MAIN_FEATURE = 0xB0
MAIN_COLLECTION = 0xA0
MAIN_END_COLLECTION = 0xC0

# Global items
GLOBAL_USAGE_PAGE = 0x04
GLOBAL_LOGICAL_MIN = 0x14
GLOBAL_LOGICAL_MAX = 0x24
GLOBAL_REPORT_SIZE = 0x74
GLOBAL_REPORT_ID = 0x84
GLOBAL_REPORT_COUNT = 0x94

# Local items
LOCAL_USAGE = 0x08
LOCAL_USAGE_MIN = 0x18
LOCAL_USAGE_MAX = 0x28

# Usage pages
PAGE_GENERIC_DESKTOP = 0x01
PAGE_BUTTON = 0x09
PAGE_CONSUMER = 0x0C

# Usages
USAGE_MOUSE = 0x02
USAGE_X = 0x30
USAGE_Y = 0x31
USAGE_WHEEL = 0x38
USAGE_CONSUMER_AC_PAN = 0x0238

def parse_descriptor(descriptor):
    """解析 HID 描述符并提取鼠标布局信息"""
    offset = 0
    state = {
        'usage_page': 0,
        'report_id': 0,
        'report_size': 0,
        'report_count': 0,
        'logical_min': 0,
        'logical_max': 0,
        'usages': [],
        'usage_min': None,
        'usage_max': None,
        'collection_depth': 0,
        'in_mouse_collection': False,
        'current_bit_offset': 0,
        'layouts': {}
    }
    
    print("=== 解析 HID 描述符 ===\n")
    
    while offset < len(descriptor):
        item, offset = parse_hid_item(descriptor, offset)
        if item is None:
            break
        
        if item.get('type') == 'LONG':
            print(f"[{offset-len(item['data'])-2:04X}] LONG item, size={len(item['data'])}")
            continue
        
        item_byte = item['byte']
        item_type = item['type']
        item_tag = item['tag']
        item_data = item['data']
        
        # 打印项信息
        item_name = "UNKNOWN"
        if item_type == ITEM_TYPE_MAIN:
            if item_tag == 0x8: item_name = "INPUT"
            elif item_tag == 0x9: item_name = "OUTPUT"
            elif item_tag == 0xB: item_name = "FEATURE"
            elif item_tag == 0xA: item_name = "COLLECTION"
            elif item_tag == 0xC: item_name = "END_COLLECTION"
        elif item_type == ITEM_TYPE_GLOBAL:
            if item_tag == 0x0: item_name = "USAGE_PAGE"
            elif item_tag == 0x1: item_name = "LOGICAL_MIN"
            elif item_tag == 0x2: item_name = "LOGICAL_MAX"
            elif item_tag == 0x7: item_name = "REPORT_SIZE"
            elif item_tag == 0x8: item_name = "REPORT_ID"
            elif item_tag == 0x9: item_name = "REPORT_COUNT"
        elif item_type == ITEM_TYPE_LOCAL:
            if item_tag == 0x0: item_name = "USAGE"
            elif item_tag == 0x1: item_name = "USAGE_MIN"
            elif item_tag == 0x2: item_name = "USAGE_MAX"
        
        data_str = ' '.join(f'{b:02X}' for b in item_data)
        print(f"[{offset-len(item_data)-1:04X}] {item_name:15} [{item_byte:02X}] data={data_str}", end='')
        
        # 处理全局项
        if item_type == ITEM_TYPE_GLOBAL:
            if item_tag == 0x0:  # USAGE_PAGE
                state['usage_page'] = decode_uint(item_data)
                print(f" -> Usage Page: 0x{state['usage_page']:04X}")
            elif item_tag == 0x1:  # LOGICAL_MIN
                state['logical_min'] = decode_uint(item_data, signed=True)
                print(f" -> Logical Min: {state['logical_min']}")
            elif item_tag == 0x2:  # LOGICAL_MAX
                state['logical_max'] = decode_uint(item_data, signed=True)
                print(f" -> Logical Max: {state['logical_max']}")
            elif item_tag == 0x7:  # REPORT_SIZE
                state['report_size'] = decode_uint(item_data)
                print(f" -> Report Size: {state['report_size']} bits")
            elif item_tag == 0x8:  # REPORT_ID
                state['report_id'] = decode_uint(item_data)
                print(f" -> Report ID: {state['report_id']}")
                # 切换到新的报告布局
                if state['report_id'] not in state['layouts']:
                    state['layouts'][state['report_id']] = {
                        'report_id': state['report_id'],
                        'buttons_bit_offset': 0,
                        'buttons_count': 0,
                        'x_bit_offset': 0,
                        'x_size': 0,
                        'y_bit_offset': 0,
                        'y_size': 0,
                        'wheel_bit_offset': 0,
                        'wheel_size': 0,
                        'pan_bit_offset': 0,
                        'pan_size': 0,
                        'current_bit_offset': 0
                    }
                state['current_bit_offset'] = 0
            elif item_tag == 0x9:  # REPORT_COUNT
                state['report_count'] = decode_uint(item_data)
                print(f" -> Report Count: {state['report_count']}")
            else:
                print()
        
        # 处理局部项
        elif item_type == ITEM_TYPE_LOCAL:
            if item_tag == 0x0:  # USAGE
                usage_page, usage = decode_usage(item_data)
                if usage_page == 0:
                    usage_page = state['usage_page']
                state['usages'].append((usage_page, usage, usage))
                print(f" -> Usage: Page=0x{usage_page:04X}, Usage=0x{usage:04X}")
            elif item_tag == 0x1:  # USAGE_MIN
                usage_page, usage = decode_usage(item_data)
                if usage_page == 0:
                    usage_page = state['usage_page']
                state['usage_min'] = (usage_page, usage)
                print(f" -> Usage Min: Page=0x{usage_page:04X}, Usage=0x{usage:04X}")
            elif item_tag == 0x2:  # USAGE_MAX
                usage_page, usage = decode_usage(item_data)
                if usage_page == 0:
                    usage_page = state['usage_page']
                if state['usage_min']:
                    min_page, min_usage = state['usage_min']
                    if min_page == usage_page:
                        state['usages'].append((usage_page, min_usage, usage))
                        print(f" -> Usage Max: Page=0x{usage_page:04X}, Usage=0x{usage:04X} (range: 0x{min_usage:04X}-0x{usage:04X})")
                    state['usage_min'] = None
                else:
                    state['usages'].append((usage_page, usage, usage))
                    print(f" -> Usage Max: Page=0x{usage_page:04X}, Usage=0x{usage:04X}")
            else:
                print()
        
        # 处理主项
        elif item_type == ITEM_TYPE_MAIN:
            if item_tag == 0xA:  # COLLECTION
                collection_type = item_data[0] if len(item_data) > 0 else 0
                state['collection_depth'] += 1
                print(f" -> Collection Type: {collection_type}")
                # 检查是否是鼠标集合
                for usage_page, usage_min, usage_max in state['usages']:
                    if usage_page == PAGE_GENERIC_DESKTOP and usage_min == USAGE_MOUSE:
                        state['in_mouse_collection'] = True
                        print(f"    -> Found MOUSE collection!")
            elif item_tag == 0xC:  # END_COLLECTION
                state['collection_depth'] -= 1
                if state['collection_depth'] == 0:
                    state['in_mouse_collection'] = False
                print(f" -> End Collection (depth={state['collection_depth']})")
            elif item_tag == 0x8:  # INPUT
                flags = decode_uint(item_data)
                is_variable = (flags & 0x02) != 0
                is_relative = (flags & 0x01) != 0
                bit_size = state['report_size'] * state['report_count']
                
                print(f" -> INPUT: flags=0x{flags:02X}, variable={is_variable}, relative={is_relative}, bit_size={bit_size}, bit_offset={state['current_bit_offset']}")
                
                # 处理 usages
                layout = state['layouts'].get(state['report_id'], None)
                if layout is None:
                    layout = {
                        'report_id': state['report_id'],
                        'buttons_bit_offset': 0,
                        'buttons_count': 0,
                        'x_bit_offset': 0,
                        'x_size': 0,
                        'y_bit_offset': 0,
                        'y_size': 0,
                        'wheel_bit_offset': 0,
                        'wheel_size': 0,
                        'pan_bit_offset': 0,
                        'pan_size': 0,
                        'current_bit_offset': 0
                    }
                    state['layouts'][state['report_id']] = layout
                
                # 处理每个 usage
                # 对于 variable 字段，每个 usage 对应 report_count 中的一项
                usage_index = 0
                for usage_page, usage_min, usage_max in state['usages']:
                    print(f"    -> Processing usage[{usage_index}]: Page=0x{usage_page:04X}, Range=0x{usage_min:04X}-0x{usage_max:04X}")
                    
                    # 计算该 usage 的位偏移
                    field_bit_offset = state['current_bit_offset']
                    if is_variable and usage_index < state['report_count']:
                        field_bit_offset = state['current_bit_offset'] + (usage_index * state['report_size'])
                        print(f"       -> Variable field: usage_index={usage_index}, field_bit_offset={field_bit_offset}")
                    
                    # 按钮
                    if usage_page == PAGE_BUTTON and usage_min >= 1:
                        if layout['buttons_count'] == 0:
                            layout['buttons_bit_offset'] = state['current_bit_offset']
                        if is_variable:
                            layout['buttons_count'] = max(layout['buttons_count'], state['report_count'])
                        else:
                            layout['buttons_count'] = max(layout['buttons_count'], usage_max - usage_min + 1)
                        print(f"       -> Buttons: offset={layout['buttons_bit_offset']}, count={layout['buttons_count']}")
                    
                    # X 轴 - 只要求 variable，不要求 relative
                    if usage_page == PAGE_GENERIC_DESKTOP and usage_min == USAGE_X and usage_max == USAGE_X:
                        if is_variable:
                            layout['x_bit_offset'] = field_bit_offset
                            layout['x_size'] = state['report_size']
                            print(f"       -> X axis: offset={layout['x_bit_offset']}, size={layout['x_size']}, is_relative={is_relative}")
                    
                    # Y 轴 - 只要求 variable，不要求 relative
                    if usage_page == PAGE_GENERIC_DESKTOP and usage_min == USAGE_Y and usage_max == USAGE_Y:
                        if is_variable:
                            layout['y_bit_offset'] = field_bit_offset
                            layout['y_size'] = state['report_size']
                            print(f"       -> Y axis: offset={layout['y_bit_offset']}, size={layout['y_size']}, is_relative={is_relative}")
                    
                    # 滚轮 - 只要求 variable
                    if usage_page == PAGE_GENERIC_DESKTOP and usage_min == USAGE_WHEEL and usage_max == USAGE_WHEEL:
                        if is_variable:
                            layout['wheel_bit_offset'] = field_bit_offset
                            layout['wheel_size'] = state['report_size']
                            print(f"       -> Wheel: offset={layout['wheel_bit_offset']}, size={layout['wheel_size']}, is_relative={is_relative}")
                    
                    # 平移 - 只要求 variable
                    if usage_page == PAGE_CONSUMER and usage_min == USAGE_CONSUMER_AC_PAN and usage_max == USAGE_CONSUMER_AC_PAN:
                        if is_variable:
                            layout['pan_bit_offset'] = field_bit_offset
                            layout['pan_size'] = state['report_size']
                            print(f"       -> Pan: offset={layout['pan_bit_offset']}, size={layout['pan_size']}, is_relative={is_relative}")
                    
                    # 递增 usage_index（仅对 variable 字段）
                    if is_variable:
                        usage_index += 1
                
                # 更新位偏移
                state['current_bit_offset'] += bit_size
                layout['current_bit_offset'] = state['current_bit_offset']
                
                # 清空 usages
                state['usages'] = []
            else:
                print()
        else:
            print()
    
    print("\n=== 解析结果 ===\n")
    for report_id, layout in sorted(state['layouts'].items()):
        print(f"Report ID {report_id}:")
        print(f"  Buttons: offset={layout['buttons_bit_offset']}, count={layout['buttons_count']}")
        print(f"  X: offset={layout['x_bit_offset']}, size={layout['x_size']}")
        print(f"  Y: offset={layout['y_bit_offset']}, size={layout['y_size']}")
        print(f"  Wheel: offset={layout['wheel_bit_offset']}, size={layout['wheel_size']}")
        print(f"  Pan: offset={layout['pan_bit_offset']}, size={layout['pan_size']}")
        print(f"  Total bits: {layout['current_bit_offset']}")
        print()
    
    return state['layouts']

if __name__ == '__main__':
    descriptor = parse_hex(descriptor_hex)
    print(f"描述符长度: {len(descriptor)} 字节\n")
    layouts = parse_descriptor(descriptor)

