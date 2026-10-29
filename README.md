# SNIFFER_CAN_LILYGO

Sniffer CAN clássico para LilyGo T-CAN485 / XY_32_CAN+RS485 V1.1, placa marcada 2024-04-25. Firmware 0.1.1, desenvolvido e utilizado em bancada para coletar comunicação entre terminal/GPS e módulos agrícolas. Não atribui automaticamente origem dos quadros a cada equipamento.

## Conteúdo
- `SNIFFER_CAN.ino`: firmware ESP32 em listen-only.
- `capture.py`: coletor USB/serial para o computador, sem interface gráfica.
- `requirements.txt`: pyserial 3.5.
- `exemplo_FICTICIO.txt`: exemplo declarado fictício, sem dados reais de equipamento.
- `VERSAO.json`: versões e hashes dos arquivos do snapshot.

Capturas reais, dados do usuário, imagens do equipamento, backups e resultados de compilação não são publicados neste repositório.

## Hardware e pinagem
Configuração usada em bancada: ESP32-D0WDQ6-V3, controlador CAN/TWAI interno e transceptor da família indicada pela LilyGo (SN65HVD231). O pinout adotado foi testado na unidade V1.1 informada acima; confira sua revisão antes de usar em outra placa.

| Função | GPIO |
|---|---:|
| CAN RX | 26 |
| CAN TX | 27 |
| CAN SE/Rs | 23 |

A placa também tem RS485 e microSD, mas ambos não são utilizados nesta versão. Não exige MCP2515 externo. Somente CAN clássico, IDs 11/29 bits e até 8 bytes; não suporta CAN FD.
Alimentação inicial somente pela USB-C. Não conectar positivo 12/24 V da máquina à placa nem assumir compatibilidade do borne DC com a alimentação dos equipamentos. Não é assumido isolamento galvânico.

## Compilar e gravar na Arduino IDE
1. Instale o pacote **esp32 por Espressif Systems, versão 3.3.10**.
2. Se necessário, URL oficial do gerenciador de placas: https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. Abra `SNIFFER_CAN.ino` nesta pasta. Selecione **ESP32 Dev Module**, flash 4 MB, PSRAM desabilitada, Core Debug None e a porta USB da placa.
4. Grave com o CAN desconectado. Se necessário, segure BOOT durante a conexão do carregador e solte quando começar a gravar.
5. Monitor serial: **921600 baud**, final de linha Nova linha. Após reset, deve aparecer:

```text
#READY,firmware=0.1.1,baud=921600,transceiver=sleep,send=START
```

Não há bibliotecas Arduino externas: TWAI, FreeRTOS e esp_timer vêm com o pacote ESP32. Compilação CLI equivalente:

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32 .
```

A compilação depende de a pasta do sketch se chamar `SNIFFER_CAN`; se clonou com nome `SNIFFER_CAN_LILYGO`, renomeie a pasta local para `SNIFFER_CAN` antes de abrir/compilar o .ino (a Arduino IDE também oferece mover o arquivo para pasta com esse nome).

## Ligação ao barramento
Use as inscrições físicas dos bornes; não deduza a orientação somente por uma foto.

```text
GPS/terminal CAN-H ================= ECU original CAN-H
                       | derivação curta -> CAN-H LilyGo
GPS/terminal CAN-L ================= ECU original CAN-L
                       | derivação curta -> CAN-L LilyGo
Referência GND da rede --------------------- GND LilyGo
Computador USB ---------------------------- USB-C LilyGo
Borne DC da LilyGo: desconectado nesta configuração
```

CAN-H não é RS485 A; CAN-L não é RS485 B. Confirme referência de terra antes de conectar. Mantenha apenas dois terminadores de 120 ohms nas extremidades da rede. Com toda alimentação desligada, aproximadamente 60 ohms entre H/L é compatível com dois terminadores em paralelo; dispositivos conectados podem afetar a medição. A placa pode ter terminação interna: conferir antes de adicionar à rede para evitar terceiro terminador. Não remover componente sem identificar sua função/revisão.

O firmware mantém transceptor desabilitado enquanto parado e só o habilita após iniciar TWAI em **TWAI_MODE_LISTEN_ONLY**: não envia quadros de dados, ACK ou quadros de erro pelo controlador. Não contém twai_transmit. A correção 0.1.1 pré-carrega TX recessivo com API GPIO IDF e evita digitalWrite antes de pinMode, que era ignorado no core ESP32 3.x e podia interferir na rede antes de START.
O software só controla os pinos após setup; reset/boot, terminação, referência elétrica e defeitos físicos também podem afetar o barramento. Se conectar a placa interromper a comunicação original, desconecte e diagnostique antes de coletar.

## Comandos locais
Não geram mensagens CAN. Sensíveis a maiúsculas, terminados por Nova linha:

```text
BITRATE 250000
START
STATUS
MARK INICIO CN10
MARK FIM CN10
STOP
```

Velocidades aceitas: 125000, 250000, 500000, 1000000 bit/s; mudar somente parado. Padrão 250000 é uma configuração, não autodetecção. Sem quadros não há evidência suficiente para escolher velocidade.
START inicia nova sessão e zera tempo/contadores. STOP desabilita transceptor, para recepção, drena quadros já aceitos e emite STATUS final seguido de #STOP. Inicia parado. O firmware do simulador é outro projeto e não pode executar simultaneamente na mesma placa.

## Gravar arquivos no computador (Windows/PowerShell)
Feche monitor Arduino e qualquer aplicativo C# que esteja usando a mesma COM. Uma aplicação por porta.
Instale Python e, na pasta do projeto, execute:

```powershell
python -m pip install -r requirements.txt
python -m serial.tools.list_ports
python capture.py --port COM5 --bitrate 250000 --out coletas\ensaio_001
```

Troque COM5 pela porta correta. A pasta de saída precisa ser nova e não é sobrescrita. O utilitário aguarda inicialização, configura a velocidade e inicia a sessão. Digite os comandos no terminal do próprio utilitário:

```text
MARK INICIO CN10
MARK FIM CN10
MARK INICIO CN11
MARK FIM CN11
STATUS
STOP
```

Aguarde #STOP antes de desconectar. Ctrl+C também tenta parar, mas encerramento abrupto/queda de energia pode deixar a sessão incompleta. Se houver erro detectado ou não houver encerramento confirmado, o utilitário cria INCOMPLETE.txt; preserve o log para diagnóstico. Em falha do leitor, pode ser necessário pressionar Enter/Ctrl+C para sair do prompt.

Arquivos gerados:

| Arquivo | Conteúdo |
|---|---|
| capture.csv | Cabeçalho e um quadro CAN por linha |
| events.csv | Timestamp e texto das marcações |
| session.log | Metadados, estatísticas, erros e encerramento |

## Formato e importação em C#
```csv
timestamp_us,id_hex,extended,rtr,dlc,data_hex
1000,001,0,0,3,0001FF
2000,18EF0012,1,0,8,0001020304050607
3000,123,0,1,4,
```

Exemplo acima **fictício**. IDs são strings hexadecimais maiúsculas, sem 0x, com 3 dígitos (padrão) ou 8 (estendido). extended/rtr são 0 ou 1. DLC bruto preservado; RTR não tem bytes e data_hex fica vazio mesmo que DLC solicite dados. Dados clássicos têm até 8 bytes, exatamente 2 dígitos por byte, incluindo zeros à esquerda. DLC fora de 0..8, se observado, exige análise específica e não autoriza acesso além dos 8 bytes.
Importe ID/data como strings em C#; não converter automaticamente para número em planilha. timestamp_us cabe em Int64.
Tempo relativo monotônico em microssegundos, obtido na tarefa ao retirar da fila TWAI. Não é timestamp físico do início/fim do quadro no fio: pode atrasar/agrupar sob carga. Eventos usam esp_timer e a mesma origem temporal, quando o comando é processado. Marca digitada tem atraso humano; não mede o contato elétrico da tecla. A ordem textual de eventos e quadros pode diferir da ordem dos timestamps devido às filas.
Na serial, linhas # são metadados/controle/diagnóstico. O coletor separa essas linhas do CSV. #SESSION registra placa/revisão, firmware, bitrate, modo e tipo de timestamp. #EVENT carrega texto CSV entre aspas; #STATUS guarda contadores e #STOP encerra.

## Perdas, erros e limites de vazão
Filas: 256 quadros no driver e 2048 na aplicação. Desacoplam recepção do envio serial e absorvem picos, mas não resolvem sobrecarga sustentada.
921600 baud 8N1 tem teto teórico de 92160 bytes/s. Com linhas de ~60 bytes, ~1500 quadros/s antes de diagnósticos/pausas. Um CAN saturado, inclusive 250 kbit/s com quadros curtos, pode exceder essa capacidade. Não há promessa de captura sem perdas em barramento carregado.

- received: quadros retirados da fila TWAI.
- written: linhas entregues ao Serial, sem confirmação de gravação pelo PC.
- app_dropped: fila da aplicação cheia.
- driver_missed / hw_overrun: contadores do driver/controlador; não presumir categorias disjuntas nem somar como total exato.
- bus_errors / REC / TEC: somente o que o controlador observa em listen-only; não são todos os problemas físicos da rede.
- pending_driver / pending_app: quadros ainda em filas.

Após STOP, queremos filas zeradas, received=written+app_dropped e contadores de perdas zerados. Isso indica ausência de perdas observadas nessas filas, não integridade comprovada de todo caminho USB/PC. Compare linhas do capture.csv com written. Zero erros não comprova velocidade/ligação corretas sem tráfego válido. O coletor não tem checksum/ACK por registro nem validação completa de corrupção serial; não usar como instrumento de conformidade/certificação.

## Teste inicial e roteiro de coleta
1. Sem CAN, confirmar READY, START/STATUS/STOP; quadros devem permanecer zero.
2. Conectar à rede de bancada já funcional, com terminação/referência corretas, ainda sem START. Verificar que a comunicação original continua.
3. START, verificar listen_only, receber quadros e preservar comunicação original. STOP não deve derrubar a rede.
4. Para verificar perdas, use gerador/analisador externo com quantidade limitada conhecida, incluindo IDs padrão/estendidos e RTR. Compare CSV e contadores; aumente carga para identificar limites. Comprove silêncio eletricamente/por analisador externo, não apenas pelo nome do modo no log.
5. Para coletar inicialização, iniciar arquivo antes de ligar os equipamentos. Repouso 20 s; marcar INICIO do CN; cinco/seis acionamentos com retornos e repousos; marcar FIM; 10 s sem ação antes do próximo CN; terminar STOP. Em tecla de dois sentidos, registrar qual sentido veio primeiro.
6. Coleta com GPS sozinho pode ter falta de ACK, retransmissões e comportamento diferente. O sniffer silencioso não fornece ACK e não torna esse cenário equivalente à rede completa.

Gravação local em microSD não implementada; é opção futura que exigirá buffers e medição de latência. Esta versão coleta somente pela USB.

## Referências oficiais
- Placa, exemplos e esquema: https://github.com/Xinyuan-LilyGO/T-CAN485
- Listen-only/TWAI ESP32: https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32/api-reference/peripherals/twai.html
- Transceptor: https://www.ti.com/product/SN65HVD231
