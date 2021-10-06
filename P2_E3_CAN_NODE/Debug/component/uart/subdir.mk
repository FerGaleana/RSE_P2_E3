################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../component/uart/fsl_adapter_uart.c 

OBJS += \
./component/uart/fsl_adapter_uart.o 

C_DEPS += \
./component/uart/fsl_adapter_uart.d 


# Each subdirectory must supply rules for building sources it contributes
component/uart/%.o: ../component/uart/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: MCU C Compiler'
	arm-none-eabi-gcc -D__REDLIB__ -DCPU_MK64FN1M0VLL12 -DCPU_MK64FN1M0VLL12_cm4 -DSDK_OS_BAREMETAL -DSERIAL_PORT_TYPE_UART=1 -DSDK_DEBUGCONSOLE=0 -DCR_INTEGER_PRINTF -DPRINTF_FLOAT_ENABLE=0 -D__MCUXPRESSO -D__USE_CMSIS -DDEBUG -DSDK_OS_FREE_RTOS -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\drivers" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\utilities" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\serial_manager" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\uart" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\lists" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\freertos\freertos_kernel\include" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\freertos\freertos_kernel\portable\GCC\ARM_CM4F" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\CMSIS" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\freertos\template\ARM_CM4F" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\device" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\board" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\source" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\drivers" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\utilities" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\serial_manager" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\uart" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\component\lists" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\CMSIS" -I"C:\MCUXpressoIDE\RSEO2021\P2_E3_CAN_NODE\device" -O0 -fno-common -g3 -Wall -c -ffunction-sections -fdata-sections -ffreestanding -fno-builtin -fmerge-constants -fmacro-prefix-map="../$(@D)/"=. -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -D__REDLIB__ -fstack-usage -specs=redlib.specs -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


