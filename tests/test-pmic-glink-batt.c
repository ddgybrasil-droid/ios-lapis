#include <stdio.h>
#include <string.h>
#include "../drivers/batt/pmic_glink_batt.h"

static int fail;
static void expect(int cond, const char *msg)
{
	if (!cond) { fprintf(stderr, "FAIL %s\n", msg); fail = 1; }
}

int main(void)
{
	unsigned char msg[LAPIS_BC_MSG_LEN];
	unsigned prop = 0, value = 0;

	expect(lapis_bc_get_req(LAPIS_BC_CAPACITY, msg) == 0, "encode");
	expect(lapis_bc_get_u32(msg) == 32778u, "owner");
	expect(lapis_bc_get_u32(msg + 4) == 1u, "type");
	expect(lapis_bc_get_u32(msg + 8) == 0x30u, "opcode");
	expect(lapis_bc_get_u32(msg + 12) == 0u, "battery id");
	expect(lapis_bc_get_u32(msg + 16) == LAPIS_BC_CAPACITY, "property");
	expect(lapis_bc_get_u32(msg + 20) == 0u, "request value");

	memset(msg, 0, sizeof(msg));
	lapis_bc_put_u32(msg + 0, LAPIS_BC_OWNER);
	lapis_bc_put_u32(msg + 4, LAPIS_BC_REQ);
	lapis_bc_put_u32(msg + 8, LAPIS_BC_GET);
	lapis_bc_put_u32(msg + 12, LAPIS_BC_CAPACITY);
	lapis_bc_put_u32(msg + 16, 5500u);
	expect(lapis_bc_parse(msg, sizeof(msg), &prop, &value) == 0, "parse");
	expect(prop == LAPIS_BC_CAPACITY && value == 5500u, "capacity wire");
	expect(lapis_bc_capacity_percent(value) == 55, "percent");
	lapis_bc_put_u32(msg + 12, LAPIS_BC_VOLT_NOW);
	lapis_bc_put_u32(msg + 16, 4200000u);
	expect(lapis_bc_parse(msg, sizeof(msg), &prop, &value) == 0, "voltage parse");
	expect(lapis_bc_voltage_mv(value) == 4200, "millivolts");
	lapis_bc_put_u32(msg + 20, 1u);
	expect(lapis_bc_parse(msg, sizeof(msg), &prop, &value) != 0, "ret_code");
	expect(lapis_bc_parse(msg, 23, &prop, &value) != 0, "short");
	expect(lapis_bc_capacity_percent(10001u) < 0, "capacity range");
	if (fail) return 1;
	printf("PASS pmic-glink\n");
	return 0;
}
