/*
 * Qualcomm battery-charger PMIC Glink property messages.
 *
 * Layout from the public qti_battery_charger client: 3 little-endian header
 * words (owner 32778, type 1, opcode 0x30 for a battery GET) followed by
 * battery id, property id and value. A response carries property id, value
 * and a return code; a non-zero return code is a failed read.
 * Capacity is the qti property divided by 100 (percent * 100 on the wire).
 * Voltage is microvolts, the same unit that driver compares with /1000.
 * This codec does not open the ADSP channel and does not invent a fuel-gauge result.
 * The caller supplies the integer types.
 */
#ifndef LAPIS_PMIC_GLINK_BATT_H
#define LAPIS_PMIC_GLINK_BATT_H

#define LAPIS_BC_OWNER     32778u
#define LAPIS_BC_REQ       1u
#define LAPIS_BC_GET       0x30u
#define LAPIS_BC_MSG_LEN   24u
#define LAPIS_BC_CAPACITY  4u
#define LAPIS_BC_VOLT_NOW  7u
#define LAPIS_BC_CURR_NOW  9u

static inline void lapis_bc_put_u32(unsigned char *p, unsigned v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static inline unsigned lapis_bc_get_u32(const unsigned char *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8) |
	       ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static inline int lapis_bc_get_req(unsigned property, unsigned char out[LAPIS_BC_MSG_LEN])
{
	unsigned i;
	if (!out) return -1;
	for (i = 0; i < LAPIS_BC_MSG_LEN; i++) out[i] = 0;
	lapis_bc_put_u32(out + 0, LAPIS_BC_OWNER);
	lapis_bc_put_u32(out + 4, LAPIS_BC_REQ);
	lapis_bc_put_u32(out + 8, LAPIS_BC_GET);
	lapis_bc_put_u32(out + 16, property);
	return 0;
}

static inline int lapis_bc_parse(const unsigned char *msg, unsigned len,
                                 unsigned *property, unsigned *value)
{
	unsigned code;
	if (!msg || !property || !value || len != LAPIS_BC_MSG_LEN) return -1;
	if (lapis_bc_get_u32(msg + 0) != LAPIS_BC_OWNER) return -1;
	if (lapis_bc_get_u32(msg + 4) != LAPIS_BC_REQ) return -1;
	if (lapis_bc_get_u32(msg + 8) != LAPIS_BC_GET) return -1;
	code = lapis_bc_get_u32(msg + 20);
	if (code) return -1;
	*property = lapis_bc_get_u32(msg + 12);
	*value = lapis_bc_get_u32(msg + 16);
	return 0;
}

static inline int lapis_bc_capacity_percent(unsigned wire)
{
	if (wire > 10000u) return -1;
	return (int)(wire / 100u);
}

static inline int lapis_bc_voltage_mv(unsigned wire)
{
	if (wire / 1000u > 10000u) return -1;
	return (int)(wire / 1000u);
}
#endif
