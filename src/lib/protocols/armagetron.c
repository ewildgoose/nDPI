/*
 * armagetron.c
 *
 * Copyright (C) 2009-11 - ipoque GmbH
 * Copyright (C) 2011-22 - ntop.org
 *
 * This file is part of nDPI, an open source deep packet inspection
 * library based on the OpenDPI and PACE technology by ipoque GmbH
 *
 * nDPI is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nDPI is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nDPI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ndpi_protocol_ids.h"

#define NDPI_CURRENT_PROTO NDPI_PROTOCOL_ARMAGETRON

#include "ndpi_api.h"


static void ndpi_int_armagetron_add_connection(struct ndpi_detection_module_struct *ndpi_struct,
					       struct ndpi_flow_struct *flow)
{
  ndpi_set_detected_protocol(ndpi_struct, flow, NDPI_PROTOCOL_ARMAGETRON, NDPI_PROTOCOL_UNKNOWN, NDPI_CONFIDENCE_DPI);
}

static void ndpi_search_armagetron_udp(struct ndpi_detection_module_struct *ndpi_struct, struct ndpi_flow_struct *flow)
{
  struct ndpi_packet_struct *packet = ndpi_get_packet_struct(ndpi_struct);

  NDPI_LOG_DBG(ndpi_struct, "search armagetron\n");

  if (packet->payload_packet_len > 10) {
    /* login request */
    if (get_u_int32_t(packet->payload, 0) == htonl(0x000b0000)) {
      const u_int16_t dataLength = ntohs(get_u_int16_t(packet->payload, 4));
      if (dataLength == 0 || dataLength * 2 + 8 != packet->payload_packet_len)
	goto exclude;
      if (get_u_int16_t(packet->payload, 6) == htons(0x0008)
	  && get_u_int16_t(packet->payload, packet->payload_packet_len - 2) == 0) {
	NDPI_LOG_INFO(ndpi_struct, "found armagetron\n");
	ndpi_int_armagetron_add_connection(ndpi_struct, flow);
	return;
      }
    }
    /* sync_msg */
    if (packet->payload_packet_len == 16 && get_u_int16_t(packet->payload, 0) == htons(0x001c)
	&& get_u_int16_t(packet->payload, 2) != 0) {
      const u_int16_t dataLength = ntohs(get_u_int16_t(packet->payload, 4));
      if (dataLength != 4)
	goto exclude;
      if (get_u_int32_t(packet->payload, 6) == htonl(0x00000500) && get_u_int32_t(packet->payload, 6 + 4) == htonl(0x00010000)
	  && get_u_int16_t(packet->payload, packet->payload_packet_len - 2) == 0) {
	NDPI_LOG_INFO(ndpi_struct, "found armagetron\n");
	ndpi_int_armagetron_add_connection(ndpi_struct, flow);
	return;
      }
    }

    /* net_sync combination */
    if (packet->payload_packet_len > 50 && get_u_int16_t(packet->payload, 0) == htons(0x0018)
	&& get_u_int16_t(packet->payload, 2) != 0) {
      u_int16_t val;
      const u_int16_t dataLength = ntohs(get_u_int16_t(packet->payload, 4));
      if (dataLength == 0 || dataLength * 2 + 8 > packet->payload_packet_len)
	goto exclude;
      val = get_u_int16_t(packet->payload, 6 + 2);
      if (val == get_u_int16_t(packet->payload, 6 + 6)) {
	val = ntohs(get_u_int16_t(packet->payload, 6 + 8));
	if ((6 + 10 + val + 4) < packet->payload_packet_len
	    && (get_u_int32_t(packet->payload, 6 + 10 + val) == htonl(0x00010000)
		|| get_u_int32_t(packet->payload, 6 + 10 + val) == htonl(0x00000001))
	    && get_u_int16_t(packet->payload, packet->payload_packet_len - 2) == 0) {
	  NDPI_LOG_INFO(ndpi_struct, "found armagetron\n");
	  ndpi_int_armagetron_add_connection(ndpi_struct, flow);
	  return;
	}
      }
    }
  }

 exclude:
  NDPI_EXCLUDE_PROTO(ndpi_struct, flow);
}



void init_armagetron_dissector(struct ndpi_detection_module_struct *ndpi_struct, u_int32_t *id)
{
  ndpi_set_bitmask_protocol_detection("Armagetron", ndpi_struct, *id,
				      NDPI_PROTOCOL_ARMAGETRON,
				      ndpi_search_armagetron_udp,
				      NDPI_SELECTION_BITMASK_PROTOCOL_V4_V6_UDP_WITH_PAYLOAD,
				      SAVE_DETECTION_BITMASK_AS_UNKNOWN,
				      ADD_TO_DETECTION_BITMASK);

  *id += 1;
}
