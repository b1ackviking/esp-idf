// Copyright 2015-2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// HAL for SPI Flash (non-IRAM part)
// The IRAM part is in spi_flash_hal_iram.c, spi_flash_hal_gpspi.c, spi_flash_hal_common.inc.

#include <stdlib.h>
#include "hal/spi_flash_hal.h"
#include "string.h"
#include "soc/soc_caps.h"
#include "hal/hal_defs.h"

#define APB_CYCLE_NS   (1000*1000*1000LL/APB_CLK_FREQ)

static const char TAG[] = "FLASH_HAL";

typedef struct {
    int div;
    spi_flash_ll_clock_reg_t clock_reg_val;
} spi_flash_hal_clock_config_t;




static const spi_flash_hal_clock_config_t spi_flash_clk_cfg_reg[ESP_FLASH_SPEED_MAX] = {
    {16,    SPI_FLASH_LL_CLKREG_VAL_5MHZ},
    {8,     SPI_FLASH_LL_CLKREG_VAL_10MHZ},
    {4,     SPI_FLASH_LL_CLKREG_VAL_20MHZ},
    {3,     SPI_FLASH_LL_CLKREG_VAL_26MHZ},
    {2,     SPI_FLASH_LL_CLKREG_VAL_40MHZ},
    {1,     SPI_FLASH_LL_CLKREG_VAL_80MHZ},
};

#if !CONFIG_IDF_TARGET_ESP32
static const spi_flash_hal_clock_config_t spi_flash_gpspi_clk_cfg_reg[ESP_FLASH_SPEED_MAX] = {
    {16,    {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_5MHZ}},
    {8,     {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_10MHZ}},
    {4,     {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_20MHZ}},
    {3,     {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_26MHZ}},
    {2,     {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_40MHZ}},
    {1,     {.gpspi=GPSPI_FLASH_LL_CLKREG_VAL_80MHZ}},
};
#else
#define spi_flash_gpspi_clk_cfg_reg spi_flash_clk_cfg_reg
#endif

static inline int get_dummy_n(bool gpio_is_used, int input_delay_ns, int eff_clk)
{
    const int apbclk_kHz = APB_CLK_FREQ / 1000;
    //calculate how many apb clocks a period has
    const int apbclk_n = APB_CLK_FREQ / eff_clk;
    const int gpio_delay_ns = gpio_is_used ? GPIO_MATRIX_DELAY_NS : 0;

    //calculate how many apb clocks the delay is, the 1 is to compensate in case ``input_delay_ns`` is rounded off.
    int apb_period_n = (1 + input_delay_ns + gpio_delay_ns) * apbclk_kHz / 1000 / 1000;
    if (apb_period_n < 0) {
        apb_period_n = 0;
    }

    return apb_period_n / apbclk_n;
}

esp_err_t spi_flash_hal_init(spi_flash_hal_context_t *data_out, const spi_flash_hal_config_t *cfg)
{
    if (!esp_ptr_internal(data_out) && cfg->host_id == SPI1_HOST) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cfg->cs_num >= SOC_SPI_PERIPH_CS_NUM(cfg->host_id)) {
        return ESP_ERR_INVALID_ARG;
    }

    bool gpspi = (cfg->host_id > SPI1_HOST);
    const spi_flash_hal_clock_config_t *clock_cfg = gpspi? &spi_flash_gpspi_clk_cfg_reg[cfg->speed]: &spi_flash_clk_cfg_reg[cfg->speed];

    *data_out = (spi_flash_hal_context_t) {
        .inst = data_out->inst, // Keeps the function pointer table
        .spi = spi_flash_ll_get_hw(cfg->host_id),
        .cs_num = cfg->cs_num,
        .extra_dummy = get_dummy_n(!cfg->iomux, cfg->input_delay_ns, APB_CLK_FREQ/clock_cfg->div),
        .clock_conf = clock_cfg->clock_reg_val,
        .cs_hold = cfg->cs_hold,
    };
    if (cfg->auto_sus_en) {
        data_out->flags |= SPI_FLASH_HOST_CONTEXT_FLAG_AUTO_SUSPEND;
        data_out->flags |= SPI_FLASH_HOST_CONTEXT_FLAG_AUTO_RESUME;
    }

    ESP_EARLY_LOGD(TAG, "extra_dummy: %d", data_out->extra_dummy);
    return ESP_OK;
}

bool spi_flash_hal_supports_direct_write(spi_flash_host_inst_t *host, const void *p)
{
    bool direct_write = ( ((spi_flash_hal_context_t *)host)->spi != spi_flash_ll_get_hw(SPI1_HOST)
                          || esp_ptr_in_dram(p) );
    return direct_write;
}


bool spi_flash_hal_supports_direct_read(spi_flash_host_inst_t *host, const void *p)
{
  //currently the host doesn't support to read through dma, no word-aligned requirements
    bool direct_read = ( ((spi_flash_hal_context_t *)host)->spi != spi_flash_ll_get_hw(SPI1_HOST)
                         || esp_ptr_in_dram(p) );
    return direct_read;
}
