/*
 * Copyright (C) 2008-2012 FreeIPMI Core Team
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#if STDC_HEADERS
#include <string.h>
#endif /* STDC_HEADERS */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <limits.h>
#include <assert.h>

#include <freeipmi/freeipmi.h>

#include "ipmi-oem.h"
#include "ipmi-oem-argp.h"
#include "ipmi-oem-common.h"
#include "ipmi-oem-quanta.h"
#include "ipmi-oem-thirdparty.h"

#include "freeipmi-portability.h"
#include "pstdout.h"

/* Quanta S99Q/Dell FS12-TY Notes
 *
 * Update Firmware Request
 *
 * 0x08 - network function
 * 0x01 - OEM cmd
 * 0x?? - Interface
 *      - 00h - system interface (e.g. kcs)
 *      - 01h - networking (e.g. tftp, ftp, http)
 *      - 02h - usb msc, legacy
 *      - 03h - USB init command
 * 0x?? - update type
 *      - [7] - force update
 *            - 0h - normal
 *            - 1h - forced
 *      - [6:3] - reserved
 *      - [2:0] - entity
 * bytes 4:15 - install options (vendor)
 *
 * Update Firmware Response
 *
 * 0x08 - network function
 * 0x01 - command
 * 0x?? - completion code
 * 0x?? - task ID
 *
 * Get Update Status Request
 *
 * 0x08 - network function
 * 0x02 - OEM cmd
 * 0x?? - task ID
 * 
 * Get Update Status Response
 *      
 * 0x08 - network function
 * 0x02 - OEM cmd
 * 0x?? - completion code
 * 0x?? - status
 *      - 00h - Transmitting Image
 *      - 01h - Validating Image
 *      - 02h - Programming
 *      - 03h - Ready to Accept Image
 *      - 04h - USB Init stage
 *      - 05h - Connecting to server
 *      - 80h - General error
 *      - 81h - Cannot establish connection
 *      - 82h - Path not found
 *      - 83h - Transmission Abort
 *      - 84h - Checksum error
 *      - 85h - Incorrect Platform
 *      - 86h - Allocate memory failed
 *      - 87h - Virtual media detach
 *      - FFh - Completed
 * 0x?? - progression indicator (optional)
 *
 * Copy Image Data Request
 *
 * 0x08 - network function
 * 0x03 - OEM cmd
 * 0x?? - In Progress
 *      - 00h - data transmission is in progress
 *      - 01h - data transmission completed
 * bytes 3:6 - image offset to be copied
 * bytes 7:N - image data to be copied
 *
 * Copy Image Data Response
 *
 * 0x03 - OEM cmd
 * 0x?? - Completion Code
 *
 * USB Firmware Update Request
 *
 * 0x08 - network function
 * 0x04 - OEM cmd
 * 0x?? - Task ID
 * 0x?? - Execution phases
 *      - 00h - phase one, emulate virtual disk
 *      - 01h - phase two, un-mount virtual disk and then start firmware update
 *
 * USB Firmware Update Response
 *
 * 0x04 - OEM cmd
 * 0x?? - Completion Code
 *
 * Get Sensor Temperature Reading Request
 *
 * 0x30 - network function
 * 0x10 - OEM cmd
 * 0x?? - sensor number
 *
 * Get Sensor Temperature Reading Response
 *
 * 0x10 - OEM cmd
 * 0x?? - Completion Code
 * 0x?? - sensor temperature reading
 *
 * Set Processor Information Request
 *
 * 0x30 - network function
 * 0x17 - OEM cmd
 * 0x?? - Processor Index, 1 based
 * 0x?? - Processor Type
 * bytes 4-5 - Processor frequency in MHZ (LSB first)
 *
 * Set Processor Information Response
 *
 * 0x17 - OEM cmd
 * 0x?? - Completion Code
 */

#define IPMI_OEM_QUANTA_MAC_ADDRESS_BYTES            6

#define IPMI_OEM_QUANTA_MAC_ADDRESS_BUS_ID           2
#define IPMI_OEM_QUANTA_MAC_ADDRESS_CHANNEL          0
#define IPMI_OEM_QUANTA_MAC_ADDRESS_SLAVE_ADDRESS    0x55
#define IPMI_OEM_QUANTA_MAC_ADDRESS_BASE_OFFSET      0x0000
#define IPMI_OEM_QUANTA_MAC_ADDRESS_DEDICATED_OFFSET 0x0000
#define IPMI_OEM_QUANTA_MAC_ADDRESS_SHARED_OFFSET    0x0006

int
ipmi_oem_quanta_get_nic_mode (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  return (ipmi_oem_thirdparty_get_nic_mode (state_data));
}

int
ipmi_oem_quanta_set_nic_mode (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 1);

  return (ipmi_oem_thirdparty_set_nic_mode (state_data));
}

int
ipmi_oem_quanta_get_bmc_services (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  return (ipmi_oem_thirdparty_get_bmc_services_v15 (state_data));
}

int
ipmi_oem_quanta_set_bmc_services (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 2);
  
  return (ipmi_oem_thirdparty_set_bmc_services_v15 (state_data));
}

int
ipmi_oem_quanta_get_account_status (ipmi_oem_state_data_t *state_data)
{
  fiid_obj_t get_user_access_obj_cmd_rs = NULL;
  fiid_obj_t get_user_name_obj_cmd_rs = NULL;
  uint32_t tmpvalue;
  uint8_t lan_channel_number;
  uint8_t number_of_users;
  uint64_t val;
  unsigned int i;
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  /* achu:
   *
   * IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_ACCOUNT_STATUS_NUMBER_OF_USER
   * returns the number of enabled/disabled users, not the total,
   * which is not what we need in this function.
   */

  if (!(get_user_access_obj_cmd_rs = fiid_obj_create (tmpl_cmd_get_user_access_rs)))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_create: %s\n",
                       strerror (errno));
      goto cleanup;
    }

  if (ipmi_get_channel_number (state_data->ipmi_ctx,
                               IPMI_CHANNEL_MEDIUM_TYPE_LAN_802_3,
                               &lan_channel_number) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_get_channel_number: %s\n",
                       ipmi_ctx_errormsg (state_data->ipmi_ctx));
      goto cleanup;
    }
  
  if (ipmi_cmd_get_user_access (state_data->ipmi_ctx,
                                lan_channel_number,
                                1, /* user_id number - any will do for this call */
                                get_user_access_obj_cmd_rs) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_cmd_get_user_access: %s\n",
                       ipmi_ctx_errormsg (state_data->ipmi_ctx));
      goto cleanup;
    }

  if (FIID_OBJ_GET (get_user_access_obj_cmd_rs,
                    "max_channel_user_ids",
                    &val) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "FIID_OBJ_GET: 'max_channel_user_ids': %s\n",
                       fiid_obj_errormsg (get_user_access_obj_cmd_rs));
      goto cleanup;
    }

  if (!(get_user_name_obj_cmd_rs = fiid_obj_create (tmpl_cmd_get_user_name_rs)))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_create: %s\n",
                       strerror (errno));
      goto cleanup;
    }

  number_of_users = val;
  
  pstdout_printf (state_data->pstate,
                  "Username         | Status\n");
  
  for (i = 0; i < number_of_users; i++)
    {
      char user_name[IPMI_MAX_USER_NAME_LENGTH + 1];
      uint8_t account_status;
      char *account_status_str = NULL;

      memset (user_name, '\0', IPMI_MAX_USER_NAME_LENGTH + 1);

      /* achu:
       *
       * Use normal IPMI get username function instead of OEM one,
       * it's safer and doesn't use P-strings.
       */

      if (fiid_obj_clear (get_user_name_obj_cmd_rs) < 0)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "fiid_obj_clear: %s\n",
                           fiid_obj_errormsg (get_user_name_obj_cmd_rs));
          goto cleanup;
        }
      
      if (ipmi_cmd_get_user_name (state_data->ipmi_ctx,
                                  i + 1,
                                  get_user_name_obj_cmd_rs) < 0)
        {
          /* Username is not set yet */
          if (ipmi_ctx_errnum (state_data->ipmi_ctx) == IPMI_ERR_BAD_COMPLETION_CODE
              && (ipmi_check_completion_code (get_user_name_obj_cmd_rs,
                                              IPMI_COMP_CODE_INVALID_DATA_FIELD_IN_REQUEST) == 1))
            continue;

          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "ipmi_cmd_get_user_name: %s\n",
                           ipmi_ctx_errormsg (state_data->ipmi_ctx));
          goto cleanup;
        }

      if (fiid_obj_get_data (get_user_name_obj_cmd_rs,
                             "user_name",
                             user_name,
                             IPMI_MAX_USER_NAME_LENGTH) < 0)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "fiid_obj_get_data: 'user_name': %s\n",
                           fiid_obj_errormsg (get_user_name_obj_cmd_rs));
          goto cleanup;
        }
      
      /* if not the first user id - but username is empty, skip output */
      if (i && !strlen (user_name))
        continue;

      if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
							 IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_ACCOUNT_STATUS,
							 IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_ACCOUNT_STATUS_ACCOUNT_STATUS,
							 i + 1,
							 1,
							 &tmpvalue) < 0)
        goto cleanup;
      account_status = tmpvalue;

      switch (account_status)
	{
	case IPMI_OEM_QUANTA_EXTENDED_CONFIG_ACCOUNT_STATUS_UNSPECIFIED:
	  account_status_str = "Unspecified";
	  break;
	case IPMI_OEM_QUANTA_EXTENDED_CONFIG_ACCOUNT_STATUS_ENABLED:
	  account_status_str = "Enabled";
	  break;
	case IPMI_OEM_QUANTA_EXTENDED_CONFIG_ACCOUNT_STATUS_DISABLED:
	  account_status_str = "Disabled";
	  break;
	case IPMI_OEM_QUANTA_EXTENDED_CONFIG_ACCOUNT_STATUS_LOCKOUT:
	  account_status_str = "Lockout";
	  break;
	default:
	  account_status_str = "Unknown";
	}

      pstdout_printf (state_data->pstate,
                      "%-16s | %s\n",
                      user_name,
                      account_status_str);
    }

  rv = 0;
 cleanup:
  fiid_obj_destroy (get_user_access_obj_cmd_rs);
  return (rv);
}

int
ipmi_oem_quanta_get_dns_config (ipmi_oem_state_data_t *state_data)
{
  uint32_t tmpvalue;
  uint8_t dnsdhcpenable;
  uint32_t dnsserver1;
  uint32_t dnsserver2;
  uint8_t dnsregisterbmc;
  char dnsbmchostname[IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX + 1];
  uint8_t dnsdomainnamedhcpenable;
  char dnsdomainname[IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX + 1];
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  memset (dnsbmchostname, '\0', IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX + 1);
  memset (dnsdomainname, '\0', IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX + 1);

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DHCP_ENABLE,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  dnsdhcpenable = tmpvalue;

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_SERVER1,
						     0,
						     4,
						     &tmpvalue) < 0)
    goto cleanup;
  dnsserver1 = tmpvalue;

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_SERVER2,
						     0,
						     4,
						     &tmpvalue) < 0)
    goto cleanup;
  dnsserver2 = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_REGISTER_BMC,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  dnsregisterbmc = tmpvalue;

  if (ipmi_oem_thirdparty_get_extended_config_string (state_data,
						      IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						      IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_BMC_HOST_NAME,
						      0,
						      dnsbmchostname,
						      IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX) < 0)
    goto cleanup;

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DOMAIN_NAME_DHCP_ENABLE,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  dnsdomainnamedhcpenable = tmpvalue;

  if (ipmi_oem_thirdparty_get_extended_config_string (state_data,
						      IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
						      IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DOMAIN_NAME,
						      0,
						      dnsdomainname,
						      IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX) < 0)
    goto cleanup;

  pstdout_printf (state_data->pstate,
		  "DNS DHCP             : %s\n",
		  (dnsdhcpenable) ? "Enabled" : "Disabled");

  pstdout_printf (state_data->pstate,
                  "DNS Server 1         : %u.%u.%u.%u\n",
                  (dnsserver1 & 0x000000FF),
                  (dnsserver1 & 0x0000FF00) >> 8,
                  (dnsserver1 & 0x00FF0000) >> 16,
                  (dnsserver1 & 0xFF000000) >> 24);

  pstdout_printf (state_data->pstate,
                  "DNS Server 2         : %u.%u.%u.%u\n",
                  (dnsserver2 & 0x000000FF),
                  (dnsserver2 & 0x0000FF00) >> 8,
                  (dnsserver2 & 0x00FF0000) >> 16,
                  (dnsserver2 & 0xFF000000) >> 24);
  
  pstdout_printf (state_data->pstate,
		  "DNS Register BMC     : %s\n",
		  (dnsdomainnamedhcpenable) ? "Enabled" : "Disabled");
  
  pstdout_printf (state_data->pstate,
                  "DNS BMC Host Name    : %s\n",
                  dnsbmchostname);
  
  pstdout_printf (state_data->pstate,
		  "DNS Domain Name DHCP : %s\n",
		  (dnsdomainnamedhcpenable) ? "Enabled" : "Disabled");
  
  pstdout_printf (state_data->pstate,
                  "DNS Domain Name      : %s\n",
                  dnsdomainname);
  
  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_set_dns_config (ipmi_oem_state_data_t *state_data)
{
  uint8_t dnsdhcpenable = 0;
  uint32_t dnsserver1 = 0;
  uint32_t dnsserver2 = 0;
  uint8_t dnsregisterbmc = 0;
  char dnsbmchostname[IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX + 1];
  uint8_t dnsdomainnamedhcpenable = 0;
  char dnsdomainname[IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX + 1];
  int rv = -1;
  unsigned int i;

  assert (state_data);

  memset (dnsbmchostname, '\0', IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX + 1);
  memset (dnsdomainname, '\0', IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX + 1);

  if (!state_data->prog_data->args->oem_options_count)
    {
      pstdout_printf (state_data->pstate,
		      "Option: dnsdhcp=enable|disable\n"
                      "Option: dnsserver1=ipaddress\n"
                      "Option: dnsserver2=ipaddress\n"
		      "Option: dnsregisterbmc=enable|disable\n"
                      "Option: dnsbmchostname=string\n"
		      "Option: dnsdomainnamedhcp=enable|disable\n"
                      "Option: dnsdomainname=string\n");
      return (0); 
    }

  for (i = 0; i < state_data->prog_data->args->oem_options_count; i++)
    {
      char *key = NULL;
      char *value = NULL;
      
      if (ipmi_oem_parse_key_value (state_data,
                                    i,
                                    &key,
                                    &value) < 0)
        goto cleanup;

      if (!strcasecmp (key, "dnsdhcp"))
        {
          if (ipmi_oem_parse_enable (state_data, i, value, &dnsdhcpenable) < 0)
            goto cleanup;
          
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DHCP_ENABLE,
							     0,
							     1,
							     (uint32_t)dnsdhcpenable) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsserver1"))
        {
          if (ipmi_oem_parse_ip_address (state_data, i, value, &dnsserver1) < 0)
            goto cleanup;
          
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_SERVER1,
							     0,
							     4,
							     (uint32_t)dnsserver1) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsserver2"))
        {
          if (ipmi_oem_parse_ip_address (state_data, i, value, &dnsserver2) < 0)
            goto cleanup;
          
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_SERVER2,
							     0,
							     4,
							     (uint32_t)dnsserver2) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsregisterbmc"))
        {
          if (ipmi_oem_parse_enable (state_data, i, value, &dnsregisterbmc) < 0)
            goto cleanup;
          
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_REGISTER_BMC,
							     0,
							     1,
							     (uint32_t)dnsregisterbmc) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsbmchostname"))
        {
          uint8_t string_length = 0;

          if (ipmi_oem_parse_string (state_data,
                                     i,
                                     value,
                                     &string_length,
                                     dnsbmchostname,
                                     IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_BMC_HOST_NAME_MAX) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_string (state_data,
							      IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							      IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_BMC_HOST_NAME,
							      0,
							      dnsbmchostname,
							      (unsigned int)string_length) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsdomainnamedhcp"))
        {
          if (ipmi_oem_parse_enable (state_data, i, value, &dnsdomainnamedhcpenable) < 0)
            goto cleanup;
          
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DOMAIN_NAME_DHCP_ENABLE,
							     0,
							     1,
							     (uint32_t)dnsdomainnamedhcpenable) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "dnsdomainname"))
        {
          uint8_t string_length = 0;

          if (ipmi_oem_parse_string (state_data,
                                     i,
                                     value,
                                     &string_length,
                                     dnsdomainname,
                                     IPMI_OEM_QUANTA_EXTENDED_CONFIG_DNS_DNS_DOMAIN_NAME_MAX) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_string (state_data,
							      IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_DNS,
							      IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_DNS_DNS_DOMAIN_NAME,
							      0,
							      dnsdomainname,
							      (unsigned int)string_length) < 0)
            goto cleanup;
        }
      else
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "%s:%s invalid OEM option argument '%s' : invalid key\n",
                           state_data->prog_data->args->oem_id,
                           state_data->prog_data->args->oem_command,
                           state_data->prog_data->args->oem_options[i]);
          goto cleanup;
        }

      free (key);
      free (value);
    }

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_get_web_server_config (ipmi_oem_state_data_t *state_data)
{
  uint32_t tmpvalue;
  uint8_t webserverenabled;
  uint8_t maxwebsessions;
  uint8_t activewebsessions;
  uint32_t webservertimeout;
  uint16_t httpportnum;
  uint16_t httpsportnum;
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_WEB_SERVER_ENABLED,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  webserverenabled = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_MAX_WEB_SESSIONS,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  maxwebsessions = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_ACTIVE_WEB_SESSIONS,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  activewebsessions = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_WEB_SERVER_TIMEOUT,
						     0,
						     4,
						     &tmpvalue) < 0)
    goto cleanup;
  webservertimeout = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_HTTP_PORT_NUM,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  httpportnum = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_HTTPS_PORT_NUM,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  httpsportnum = tmpvalue;
  
  pstdout_printf (state_data->pstate,
		  "Web Server          : %s\n",
		  (webserverenabled) ? "Enabled" : "Disabled");
  
  pstdout_printf (state_data->pstate,
		  "Max Web Sessions    : %u\n",
		  maxwebsessions);

  pstdout_printf (state_data->pstate,
		  "Active Web Sessions : %u\n",
		  activewebsessions);

  pstdout_printf (state_data->pstate,
		  "Web Server Timeout  : %u seconds\n",
		  webservertimeout);

  pstdout_printf (state_data->pstate,
		  "http Port Number    : %u\n",
		  httpportnum);

  pstdout_printf (state_data->pstate,
		  "https Port Number   : %u\n",
		  httpsportnum);

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_set_web_server_config (ipmi_oem_state_data_t *state_data)
{
  uint8_t webserverenabled = 0;
  uint32_t webservertimeout = 0;
  uint16_t httpportnumber = 0;
  uint16_t httpsportnumber = 0;
  int rv = -1;
  unsigned int i;

  assert (state_data);

  if (!state_data->prog_data->args->oem_options_count)
    {
      pstdout_printf (state_data->pstate,
		      "Option: webserver=enable|disable\n"
		      "Option: webservertimeout=seconds\n"
		      "Option: httpportnumber=num\n"
		      "Option: httpsportnumber=num\n");
      return (0); 
    }

  for (i = 0; i < state_data->prog_data->args->oem_options_count; i++)
    {
      char *key = NULL;
      char *value = NULL;
      
      if (ipmi_oem_parse_key_value (state_data,
                                    i,
                                    &key,
                                    &value) < 0)
        goto cleanup;

      if (!strcasecmp (key, "webserver"))
        {
          if (ipmi_oem_parse_enable (state_data, i, value, &webserverenabled) < 0)
            goto cleanup;

          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_WEB_SERVER_ENABLED,
							     0,
							     1,
							     (uint32_t)webserverenabled) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "webservertimeout"))
        {
          if (ipmi_oem_parse_4_byte_field (state_data, i, value, &webservertimeout) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_WEB_SERVER_TIMEOUT,
							     0,
							     4,
							     webservertimeout) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "httpportnumber"))
        {
          if (ipmi_oem_parse_2_byte_field (state_data, i, value, &httpportnumber) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_HTTP_PORT_NUM,
							     0,
							     2,
							     (uint32_t)httpportnumber) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "httpsportnumber"))
        {
          if (ipmi_oem_parse_2_byte_field (state_data, i, value, &httpsportnumber) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_WEB_SERVER_CONFIGURATION,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_WEB_SERVER_CONFIGURATION_HTTPS_PORT_NUM,
							     0,
							     2,
							     (uint32_t)httpsportnumber) < 0)
            goto cleanup;
        }
      else
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "%s:%s invalid OEM option argument '%s' : invalid key\n",
                           state_data->prog_data->args->oem_id,
                           state_data->prog_data->args->oem_command,
                           state_data->prog_data->args->oem_options[i]);
          goto cleanup;
        }

      free (key);
      free (value);
    }

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_get_power_management_config (ipmi_oem_state_data_t *state_data)
{
  uint32_t tmpvalue;
  uint8_t powermanagementenable;
  uint8_t dpnmpowermanagement;
  uint8_t powerstaggeringacrecovery;
  uint16_t powerondelay;
  uint16_t minpowerondelay;
  uint16_t maxpowerondelay;
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_MANAGEMENT_ENABLE,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  powermanagementenable = tmpvalue;

  dpnmpowermanagement = (powermanagementenable & IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_MANAGEMENT_ENABLE_DPNM_BITMASK);
  dpnmpowermanagement >>= IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_MANAGEMENT_ENABLE_DPNM_SHIFT;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_STAGGERING_AC_RECOVERY,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  powerstaggeringacrecovery = tmpvalue;
  
  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_ON_DELAY,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  powerondelay = tmpvalue;

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_MINIMUM_POWER_ON_DELAY,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  minpowerondelay = tmpvalue; 

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_MAXIMUM_POWER_ON_DELAY,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  maxpowerondelay = tmpvalue; 
  
  pstdout_printf (state_data->pstate,
		  "DPNM Power Management        : %s\n",
		  (dpnmpowermanagement) ? "Enabled" : "Disabled");
  
  if (powerstaggeringacrecovery == IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_IMMEDIATE)
    pstdout_printf (state_data->pstate,
                    "Power Staggering AC Recovery : Immediate\n");
  else if (powerstaggeringacrecovery == IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_AUTO)
    pstdout_printf (state_data->pstate,
                    "Power Staggering AC Recovery : Auto\n");
  else if (powerstaggeringacrecovery == IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_USER_DEFINED)
    pstdout_printf (state_data->pstate,
                    "Power Staggering AC Recovery : User Defined\n");
  else
    pstdout_printf (state_data->pstate,
                    "Power Staggering AC Recovery : %Xh\n",
                    powerstaggeringacrecovery);
  
  pstdout_printf (state_data->pstate,
		  "Power On Delay               : %u seconds\n",
                  powerondelay);
  
  pstdout_printf (state_data->pstate,
		  "Minimum Power On Delay       : %u seconds\n",
                  minpowerondelay);

  pstdout_printf (state_data->pstate,
		  "Maximum Power On Delay       : %u seconds\n",
                  maxpowerondelay);

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_set_power_management_config (ipmi_oem_state_data_t *state_data)
{
  uint8_t powermanagementenable = 0;
  uint8_t dpnmpowermanagement = 0;
  uint8_t powerstaggeringacrecovery = 0;
  uint16_t powerondelay = 0;
  uint16_t maxpowerondelay = 0;
  int rv = -1;
  unsigned int i;

  assert (state_data);

  if (!state_data->prog_data->args->oem_options_count)
    {
      pstdout_printf (state_data->pstate,
		      "Option: dpnmpowermanagement=enable|disable\n"
		      "Option: powerstaggeringacrecovery=immediate|auto|user\n"
		      "Option: powerondelay=seconds\n"
		      "Option: maxpowerondelay=seconds\n");
      return (0); 
    }

  for (i = 0; i < state_data->prog_data->args->oem_options_count; i++)
    {
      char *key = NULL;
      char *value = NULL;
      
      if (ipmi_oem_parse_key_value (state_data,
                                    i,
                                    &key,
                                    &value) < 0)
        goto cleanup;

      if (!strcasecmp (key, "dpnmpowermanagement"))
        {
          if (ipmi_oem_parse_enable (state_data, i, value, &dpnmpowermanagement) < 0)
            goto cleanup;

          powermanagementenable |= (dpnmpowermanagement << IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_MANAGEMENT_ENABLE_DPNM_SHIFT);
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_MANAGEMENT_ENABLE,
							     0,
							     1,
							     (uint32_t)powermanagementenable) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "powerstaggeringacrecovery"))
        {
          if (strcasecmp (value, "immediate")
              && strcasecmp (value, "auto")
              && strcasecmp (value, "user"))
            {
              pstdout_fprintf (state_data->pstate,
                               stderr,
                               "%s:%s invalid OEM option argument '%s' : invalid value\n",
                               state_data->prog_data->args->oem_id,
                               state_data->prog_data->args->oem_command,
                               state_data->prog_data->args->oem_options[i]);
              goto cleanup;
            }

          if (!strcasecmp (value, "immediate"))
            powerstaggeringacrecovery = IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_IMMEDIATE;
          else if (!strcasecmp (value, "auto"))
            powerstaggeringacrecovery = IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_AUTO;
          else /* !strcasecmp (value, "user")) */
            powerstaggeringacrecovery = IPMI_OEM_QUANTA_EXTENDED_CONFIG_POWER_STAGGERING_AC_RECOVERY_USER_DEFINED;

          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_STAGGERING_AC_RECOVERY,
							     0,
							     1,
							     (uint32_t)powerstaggeringacrecovery) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "powerondelay"))
        {
          if (ipmi_oem_parse_2_byte_field (state_data, i, value, &powerondelay) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_POWER_ON_DELAY,
							     0,
							     2,
							     (uint32_t)powerondelay) < 0)
            goto cleanup;
        }
      else if (!strcasecmp (key, "maxpowerondelay"))
        {
          if (ipmi_oem_parse_2_byte_field (state_data, i, value, &maxpowerondelay) < 0)
            goto cleanup;
          
          if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
							     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_POWER_MANAGEMENT,
							     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_POWER_MANAGEMENT_MAXIMUM_POWER_ON_DELAY,
							     0,
							     2,
							     (uint32_t)maxpowerondelay) < 0)
            goto cleanup;
        }
      else
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "%s:%s invalid OEM option argument '%s' : invalid key\n",
                           state_data->prog_data->args->oem_id,
                           state_data->prog_data->args->oem_command,
                           state_data->prog_data->args->oem_options[i]);
          goto cleanup;
        }

      free (key);
      free (value);
    }

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_get_sol_idle_timeout (ipmi_oem_state_data_t *state_data)
{
  uint32_t tmpvalue;
  uint16_t timeout;
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_SOL,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_SOL_SOL_IDLE_TIMEOUT,
						     0,
						     2,
						     &tmpvalue) < 0)
    goto cleanup;
  
  timeout = tmpvalue;

  pstdout_printf (state_data->pstate,
                  "%u minutes\n",
                  timeout);

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_set_sol_idle_timeout (ipmi_oem_state_data_t *state_data)
{
  uint16_t timeout;
  int rv = -1;
  
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 1);
  
  if (strcasecmp (state_data->prog_data->args->oem_options[0], "none"))
    {
      unsigned int temp;
      char *endptr = NULL;
      
      errno = 0;
      
      temp = strtoul (state_data->prog_data->args->oem_options[0], &endptr, 10);
      if (errno
          || endptr[0] != '\0'
          || temp > USHRT_MAX)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "%s:%s invalid OEM option argument '%s'\n",
                           state_data->prog_data->args->oem_id,
                           state_data->prog_data->args->oem_command,
                           state_data->prog_data->args->oem_options[0]);
          goto cleanup;
        }

      timeout = temp;
    }
  else
    timeout = 0;

  if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_SOL,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_SOL_SOL_IDLE_TIMEOUT,
						     0,
						     2,
						     (uint32_t)timeout) < 0)
    goto cleanup;

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_get_telnet_ssh_redirect_status (ipmi_oem_state_data_t *state_data)
{
  uint32_t tmpvalue;
  int rv = -1;

  assert (state_data);
  assert (!state_data->prog_data->args->oem_options_count);

  if (ipmi_oem_thirdparty_get_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_SOL,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_SOL_TELNET_SSH_REDIRECT_ENABLE,
						     0,
						     1,
						     &tmpvalue) < 0)
    goto cleanup;
  
  pstdout_printf (state_data->pstate,
                  "%s\n",
                  (tmpvalue) ? "enabled" : "disabled");
  
  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_set_telnet_ssh_redirect_status (ipmi_oem_state_data_t *state_data)
{
  uint8_t enable = 0;
  int rv = -1;
  
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 1);
  
  if (strcasecmp (state_data->prog_data->args->oem_options[0], "enable")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "disable"))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid OEM option argument '%s'\n",
                       state_data->prog_data->args->oem_id,
                       state_data->prog_data->args->oem_command,
                       state_data->prog_data->args->oem_options[0]);
      goto cleanup;
    }
  
  if (!strcasecmp (state_data->prog_data->args->oem_options[0], "enable"))
    enable = 1;
  else
    enable = 0;
  
  if (ipmi_oem_thirdparty_set_extended_config_value (state_data,
						     IPMI_OEM_QUANTA_EXTENDED_CONFIGURATION_ID_SOL,
						     IPMI_OEM_QUANTA_EXTENDED_ATTRIBUTE_ID_SOL_TELNET_SSH_REDIRECT_ENABLE,
						     0,
						     1,
						     (uint32_t)enable) < 0)
    goto cleanup;

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_reset_to_defaults (ipmi_oem_state_data_t *state_data)
{
  uint8_t bytes_rq[IPMI_OEM_MAX_BYTES];
  uint8_t bytes_rs[IPMI_OEM_MAX_BYTES];
  uint8_t task_id;
  int rs_len;
  int rv = -1;

  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 1);

  if (strcasecmp (state_data->prog_data->args->oem_options[0], "all")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "user")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "lan")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "sol")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "serial")
      && strcasecmp (state_data->prog_data->args->oem_options[0], "pef"))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid OEM option argument '%s'\n",
                       state_data->prog_data->args->oem_id,
                       state_data->prog_data->args->oem_command,
                       state_data->prog_data->args->oem_options[0]);
      goto cleanup;
    }

  /* Quanta S99Q/Dell FS12-TY OEM
   *
   * Request Reset To Defaults
   *
   * 0x30 - OEM network function
   * 0x04 - OEM cmd
   * 0x?? - bitmask
   *      [7:5] = 111b = restore parameters not included below
   *            = 000b = remaining parameters stay what it is
   *      [4] = 1b = restore PEFs to default
   *      [3] = 1b = restore serial configuration parameters to default
   *      [2] = 1b = restore SOL configuration parameters to default
   *      [1] = 1b = restore LAN configuration parameters to default
   *      [0] = 1b = restore user accounts to default
   *
   * Response Reset To Defaults
   *
   * 0x04 - OEM cmd
   * 0x?? - Completion Code
   *      - 0xCC - one or more configs not supported
   * 0x?? - Task ID - used to get the restore status.  Invalid after
   *        120 seconds.  00h = reserved.
   *
   * Request Get Restore Status
   *
   * 0x30 - OEM network function
   * 0x05 - OEM cmd
   * 0x?? - Task ID
   *
   * Response Get Restore Status
   *
   * 0x05 - OEM cmd
   * 0x?? - Completion Code
   * 0x?? - restore status
   *      - 00h = restore in progress
   *      - 01h = restore complete
   */

  bytes_rq[0] = IPMI_CMD_OEM_QUANTA_RESET_TO_DEFAULTS;

  bytes_rq[1] = IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_RESTORE_FLAG_REMAINING_PARAMETERS_STAY_WHAT_IT_IS;
  bytes_rq[1] <<= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_RESTORE_FLAG_SHIFT;

  if (!strcasecmp (state_data->prog_data->args->oem_options[0], "all"))
    {
      bytes_rq[1] = IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_RESTORE_FLAG_RESTORE_PARAMETERS_NOT_INCLUDED_BELOW;
      bytes_rq[1] <<= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_RESTORE_FLAG_SHIFT;
      bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_USER_ACCOUNTS_BITMASK;
      bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_LAN_CONFIGURATION_BITMASK;
      bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_SOL_CONFIGURATION_BITMASK;
      bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_SERIAL_CONFIGURATION_BITMASK;
      bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_PEF_BITMASK;
    }
  else if (!strcasecmp (state_data->prog_data->args->oem_options[0], "user"))
    bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_USER_ACCOUNTS_BITMASK;
  else if (!strcasecmp (state_data->prog_data->args->oem_options[0], "lan"))
    bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_LAN_CONFIGURATION_BITMASK;
  else if (!strcasecmp (state_data->prog_data->args->oem_options[0], "sol"))
    bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_SOL_CONFIGURATION_BITMASK;
  else if (!strcasecmp (state_data->prog_data->args->oem_options[0], "serial"))
    bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_SERIAL_CONFIGURATION_BITMASK;
  else  /* !strcasecmp (state_data->prog_data->args->oem_options[0], "pef" */
    bytes_rq[1] |= IPMI_OEM_QUANTA_RESET_TO_DEFAULTS_PEF_BITMASK;

  if ((rs_len = ipmi_cmd_raw (state_data->ipmi_ctx,
                              0, /* lun */
                              IPMI_NET_FN_OEM_QUANTA_GENERIC_RQ, /* network function */
                              bytes_rq, /* data */
                              2, /* num bytes */
                              bytes_rs,
                              IPMI_OEM_MAX_BYTES)) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_cmd_raw: %s\n",
                       ipmi_ctx_errormsg (state_data->ipmi_ctx));
      goto cleanup;
    }

  if (ipmi_oem_check_response_and_completion_code (state_data,
                                                   bytes_rs,
                                                   rs_len,
                                                   3,
                                                   IPMI_CMD_OEM_QUANTA_RESET_TO_DEFAULTS,
                                                   IPMI_NET_FN_OEM_QUANTA_GENERIC_RS,
                                                   NULL) < 0)
    goto cleanup;

  task_id = bytes_rs[2];

  /* don't quit until it is done */
  while (1)
    {
      bytes_rq[0] = IPMI_CMD_OEM_QUANTA_GET_RESTORE_STATUS;
      bytes_rq[1] = task_id;

      if ((rs_len = ipmi_cmd_raw (state_data->ipmi_ctx,
                                  0, /* lun */
                                  IPMI_NET_FN_OEM_QUANTA_GENERIC_RQ, /* network function */
                                  bytes_rq, /* data */
                                  2, /* num bytes */
                                  bytes_rs,
                                  IPMI_OEM_MAX_BYTES)) < 0)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "ipmi_cmd_raw: %s\n",
                           ipmi_ctx_errormsg (state_data->ipmi_ctx));
          goto cleanup;
        }

      if (ipmi_oem_check_response_and_completion_code (state_data,
                                                       bytes_rs,
                                                       rs_len,
                                                       3,
                                                       IPMI_CMD_OEM_QUANTA_GET_RESTORE_STATUS,
                                                       IPMI_NET_FN_OEM_QUANTA_GENERIC_RS,
                                                       NULL) < 0)
        goto cleanup;

      if (bytes_rs[2] == IPMI_OEM_QUANTA_GET_RESTORE_STATUS_RESTORE_COMPLETE)
        break;

      sleep (1);
    }

  rv = 0;
 cleanup:
  return (rv);
}

int
ipmi_oem_quanta_get_processor_information (ipmi_oem_state_data_t *state_data)
{
  uint8_t bytes_rq[IPMI_OEM_MAX_BYTES];
  uint8_t bytes_rs[IPMI_OEM_MAX_BYTES];
  uint8_t processor_index = 0;
  uint8_t processor_index_init = 1;
  uint8_t processor_index_max = 0xFF;
  uint8_t processor_type;
  char *processor_type_str = NULL;
  uint16_t processor_mhz;
  int rs_len;
  int rv = -1;

  /* Quanta S99Q/Dell FS12-TY OEM
   *
   * Request Get Processor Information
   *
   * 0x30 - OEM network function
   * 0x18 - OEM cmd
   * 0x?? - Processor Index, 1 based
   * 
   * Response Get Processor Information
   *
   * 0x18 - OEM cmd
   * 0x?? - Completion Code
   * 0x?? - Processor Type
   * bytes 3-4 - Processor frequency in MHZ (LSB first)
   */

  assert (state_data);
  
  if (state_data->prog_data->args->oem_options_count > 1)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid number of options specified\n");
      goto cleanup;
    }

  if (state_data->prog_data->args->oem_options_count)
    {
      char *endptr = NULL;
      unsigned int temp;

      errno = 0;
      temp = strtoul (state_data->prog_data->args->oem_options[0], &endptr, 10);
      if (errno
          || endptr[0] != '\0'
          || temp > UCHAR_MAX
          || !temp)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "%s:%s invalid OEM option argument '%s'\n",
                           state_data->prog_data->args->oem_id,
                           state_data->prog_data->args->oem_command,
                           state_data->prog_data->args->oem_options[0]);
          goto cleanup;
        }
      
      processor_index_init = temp;

      processor_index_max = processor_index_init;
    }

  for (processor_index = processor_index_init;
       processor_index <= processor_index_max;
       processor_index++)
    {
      bytes_rq[0] = IPMI_CMD_OEM_QUANTA_GET_PROCESSOR_INFORMATION;
      bytes_rq[1] = processor_index;

      if ((rs_len = ipmi_cmd_raw (state_data->ipmi_ctx,
                                  0, /* lun */
                                  IPMI_NET_FN_OEM_QUANTA_GENERIC_RQ, /* network function */
                                  bytes_rq, /* data */
                                  2, /* num bytes */
                                  bytes_rs,
                                  IPMI_OEM_MAX_BYTES)) < 0)
        {
          pstdout_fprintf (state_data->pstate,
                           stderr,
                           "ipmi_cmd_raw: %s\n",
                           ipmi_ctx_errormsg (state_data->ipmi_ctx));
          goto cleanup;
        }

      /* If processor index 1 fails or user input a processor index,
       * fall through to normal error output below.
       */
      if (rs_len >= 2
	  && (bytes_rs[1] == IPMI_COMP_CODE_PARAMETER_OUT_OF_RANGE
	      || bytes_rs[1] == IPMI_COMP_CODE_INVALID_DATA_FIELD_IN_REQUEST)
          && processor_index_init != processor_index_max
          && processor_index != 1)
        break;

      if (ipmi_oem_check_response_and_completion_code (state_data,
                                                       bytes_rs,
                                                       rs_len,
                                                       5,
                                                       IPMI_CMD_OEM_QUANTA_GET_PROCESSOR_INFORMATION,
                                                       IPMI_NET_FN_OEM_QUANTA_GENERIC_RS,
                                                       NULL) < 0)
        goto cleanup;

      processor_type = bytes_rs[2];

      processor_mhz = (bytes_rs[3] | (bytes_rs[4] << 8));

      if (processor_type == IPMI_OEM_QUANTA_PROCESSOR_TYPE_NO_CPU_PRESENT)
	pstdout_printf (state_data->pstate,
			"Processor %u: No CPU Present\n",
                        processor_index);
      else
	{
	  switch (processor_type)
	    {
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_CELERON:
	      processor_type_str = "Celeron";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_PENTIUM_3:
	      processor_type_str = "Pentium 3";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_PENTIUM_4:
	      processor_type_str = "Pentium 4";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_XEON:
	      processor_type_str = "Xeon";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_PRESTONIA:
	      processor_type_str = "Prestonia";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_NOCONA:
	      processor_type_str = "Nocona";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_OPTERON:
	      processor_type_str = "Opteron";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_DEMPSEY:
	      processor_type_str = "Dempsey";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_CLOVERTOWN:
	      processor_type_str = "Clovertown";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_TIGERTON:
	      processor_type_str = "Tigerton";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_DUNNINGTON:
	      processor_type_str = "Dunnington";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_HARPERTOWN:
	      processor_type_str = "Harpertown";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_WOLFDALE_DP:
	      /* achu: listed as "WolfDale-Dp" in spec, I don't like that output */
	      processor_type_str = "Wolfdale-DP";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_NEHALEM_EP:
	      processor_type_str = "Nehalem-EP";
	      break;
	    case IPMI_OEM_QUANTA_PROCESSOR_TYPE_WESTMERE_EP:
	      processor_type_str = "Westmere-EP";
	      break;
	    default:
	      processor_type_str = "Unknown Processor";
	    }
	  
	  pstdout_printf (state_data->pstate,
			  "Processor %u: %s %.2f Ghz\n",
			  processor_index,
			  processor_type_str,
			  (double)processor_mhz / 1000);
	}
    }

  rv = 0;
 cleanup:
  return (rv);
}

static int
_ipmi_oem_quanta_read_mac_address_s99q (ipmi_oem_state_data_t *state_data)
{
  fiid_obj_t obj_cmd_rs = NULL;
  uint8_t data_buf[IPMI_OEM_MAX_BYTES];
  uint8_t data_rq[IPMI_OEM_MAX_BYTES];
  int data_len;
  int rv = -1;

  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 2);

  if (strcasecmp (state_data->prog_data->args->oem_options[1], "shared")
      && strcasecmp (state_data->prog_data->args->oem_options[1], "dedicated"))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid OEM option argument '%s'\n",
                       state_data->prog_data->args->oem_id,
                       state_data->prog_data->args->oem_command,
                       state_data->prog_data->args->oem_options[1]);
      goto cleanup;
    }

  /* Uses Master-Read Write Command
   *
   * Most addresses provided directly from Dell.
   *
   * byte 1 = 5 (channel = 0, bus id = 2, bus-type = 1 = private)
   * byte 2 = 0xAA (slave address 7 bit = 0x55, lowest bit for r/w, 0b = read, 1b = write)
   * byte 3 = 0x0C - read count
   * byte 4/5 - 0x0000 - address to read, msb first
   * 
   * response
   *
   * byte 1 = comp-code
   * byte 2-7 = dedicated MAC
   * byte 8-13 = shared MAC
   */

  if (!(obj_cmd_rs = fiid_obj_create (tmpl_cmd_master_write_read_rs)))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_create: %s\n",
                       strerror (errno));
      goto cleanup;
    }

  if (fiid_obj_clear (obj_cmd_rs) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_clear: %s\n",
                       fiid_obj_errormsg (obj_cmd_rs));
      goto cleanup;
    }

  data_rq[0] = (IPMI_OEM_QUANTA_MAC_ADDRESS_BASE_OFFSET & 0xFF00) >> 8;
  data_rq[1] = (IPMI_OEM_QUANTA_MAC_ADDRESS_BASE_OFFSET & 0x00FF);

  if (ipmi_cmd_master_write_read (state_data->ipmi_ctx,
                                  IPMI_BUS_TYPE_PRIVATE,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_BUS_ID,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_CHANNEL,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_SLAVE_ADDRESS,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_BYTES * 2, /* returns two MACs */
                                  data_rq,
                                  2,
                                  obj_cmd_rs) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_cmd_master_write_read: %s\n",
                       ipmi_ctx_errormsg (state_data->ipmi_ctx));
      goto cleanup;
    }

  if ((data_len = fiid_obj_get_data (obj_cmd_rs,
                                     "data",
                                     data_buf,
                                     IPMI_OEM_MAX_BYTES)) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_get_data: 'data': %s\n",
                       fiid_obj_errormsg (obj_cmd_rs));
      goto cleanup;
    }

  if (data_len != (IPMI_OEM_QUANTA_MAC_ADDRESS_BYTES * 2))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "invalid bytes returned: %u\n",
                       data_len);
      goto cleanup;
    }

  if (!strcasecmp (state_data->prog_data->args->oem_options[1], "dedicated"))
    pstdout_printf (state_data->pstate,
                    "%02X:%02X:%02X:%02X:%02X:%02X\n",
                    data_buf[0],
                    data_buf[1],
                    data_buf[2],
                    data_buf[3],
                    data_buf[4],
                    data_buf[5]);
  else
    pstdout_printf (state_data->pstate,
                    "%02X:%02X:%02X:%02X:%02X:%02X\n",
                    data_buf[6],
                    data_buf[7],
                    data_buf[8],
                    data_buf[9],
                    data_buf[10],
                    data_buf[11]);

  rv = 0;
 cleanup:
  fiid_obj_destroy (obj_cmd_rs);
  return (rv);
}

int
ipmi_oem_quanta_read_mac_address (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 2);

  if (!strcasecmp (state_data->prog_data->args->oem_options[0], "s99q"))
    return _ipmi_oem_quanta_read_mac_address_s99q (state_data);
  
  pstdout_fprintf (state_data->pstate,
                   stderr,
                   "%s:%s invalid OEM option argument '%s'\n",
                   state_data->prog_data->args->oem_id,
                   state_data->prog_data->args->oem_command,
                   state_data->prog_data->args->oem_options[0]);
  return (-1);
}

static int
_ipmi_oem_quanta_write_mac_address_s99q (ipmi_oem_state_data_t *state_data)
{
  fiid_obj_t obj_cmd_rs = NULL;
  unsigned int b1, b2, b3, b4, b5, b6;
  uint8_t data_rq[IPMI_OEM_MAX_BYTES];
  int rv = -1;

  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 3);

  if (strcasecmp (state_data->prog_data->args->oem_options[1], "shared")
      && strcasecmp (state_data->prog_data->args->oem_options[1], "dedicated"))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid OEM option argument '%s'\n",
                       state_data->prog_data->args->oem_id,
                       state_data->prog_data->args->oem_command,
                       state_data->prog_data->args->oem_options[1]);
      goto cleanup;
    }
  
  if (sscanf (state_data->prog_data->args->oem_options[2],
              "%02x:%02x:%02x:%02x:%02x:%02x",
              &b1,
              &b2,
              &b3,
              &b4,
              &b5,
              &b6) != 6)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "%s:%s invalid OEM option argument '%s'\n",
                       state_data->prog_data->args->oem_id,
                       state_data->prog_data->args->oem_command,
                       state_data->prog_data->args->oem_options[2]);
      goto cleanup;
    }

  /* Uses Master-Read Write Command
   *
   * Most addresses provided directly from Dell.
   *
   * byte 1 = 5 (channel = 0, bus id = 2, bus-type = 1 = private)
   * byte 2 = 0xAA (slave address 7 bit = 0x55, lowest bit for r/w, 0b = read, 1b = write)
   * byte 3 = 0x00 - read count
   * byte 4/5 - 0x0000 | 0x0006 - address to read, msb first
   *          - 0x0000 for dedicated
   *          - 0x0006 for shared
   * 
   * response
   *
   * byte 1 = comp-code
   */

  if (!(obj_cmd_rs = fiid_obj_create (tmpl_cmd_master_write_read_rs)))
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_create: %s\n",
                       strerror (errno));
      goto cleanup;
    }

  if (fiid_obj_clear (obj_cmd_rs) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "fiid_obj_clear: %s\n",
                       fiid_obj_errormsg (obj_cmd_rs));
      goto cleanup;
    }

  if (!strcasecmp (state_data->prog_data->args->oem_options[1], "dedicated"))
    {
      data_rq[0] = (IPMI_OEM_QUANTA_MAC_ADDRESS_DEDICATED_OFFSET & 0xFF00) >> 8;
      data_rq[1] = (IPMI_OEM_QUANTA_MAC_ADDRESS_DEDICATED_OFFSET & 0x00FF);
    }
  else
    {
      data_rq[0] = (IPMI_OEM_QUANTA_MAC_ADDRESS_SHARED_OFFSET & 0xFF00) >> 8;
      data_rq[1] = (IPMI_OEM_QUANTA_MAC_ADDRESS_SHARED_OFFSET & 0x00FF);
    }

  data_rq[2] = b1;
  data_rq[3] = b2;
  data_rq[4] = b3;
  data_rq[5] = b4;
  data_rq[6] = b5;
  data_rq[7] = b6;

  if (ipmi_cmd_master_write_read (state_data->ipmi_ctx,
                                  IPMI_BUS_TYPE_PRIVATE,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_BUS_ID,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_CHANNEL,
                                  IPMI_OEM_QUANTA_MAC_ADDRESS_SLAVE_ADDRESS,
                                  0,
                                  data_rq,
                                  8,
                                  obj_cmd_rs) < 0)
    {
      pstdout_fprintf (state_data->pstate,
                       stderr,
                       "ipmi_cmd_master_write_read: %s\n",
                       ipmi_ctx_errormsg (state_data->ipmi_ctx));
      goto cleanup;
    }

  rv = 0;
 cleanup:
  fiid_obj_destroy (obj_cmd_rs);
  return (rv);
}

int
ipmi_oem_quanta_write_mac_address (ipmi_oem_state_data_t *state_data)
{
  assert (state_data);
  assert (state_data->prog_data->args->oem_options_count == 3);

  if (!strcasecmp (state_data->prog_data->args->oem_options[0], "s99q"))
    return _ipmi_oem_quanta_write_mac_address_s99q (state_data);
  
  pstdout_fprintf (state_data->pstate,
                   stderr,
                   "%s:%s invalid OEM option argument '%s'\n",
                   state_data->prog_data->args->oem_id,
                   state_data->prog_data->args->oem_command,
                   state_data->prog_data->args->oem_options[0]);
  return (-1);
}
