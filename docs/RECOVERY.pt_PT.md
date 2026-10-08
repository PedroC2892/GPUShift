# Recuperar de um ecrã preto

Todas as trocas de modo do GPUShift precisam de um reinício. Se o computador
voltar com o ecrã preto, ou sem o ecrã de início de sessão gráfico, siga estes
passos **por ordem**. Cada um basta por si; passe ao seguinte só se o anterior
não for possível.

Versão em inglês: [RECOVERY.md](RECOVERY.md).

## 1. Aguardar e reiniciar (automático)

Depois de uma troca de modo, o GPUShift fica à espera de confirmação. Iniciar
uma sessão gráfica confirma a troca: aparece uma pequena janela com *Manter*
ou *Reverter*, que reverte sozinha ao fim de 30 segundos sem resposta. Sem a
interface gráfica, basta chegar à sessão.

Se a troca nunca for confirmada, o serviço `gpushift-boot-check` repõe o modo
anterior no **terceiro arranque** sem confirmação: volta a pôr a configuração
anterior, regenera o initramfs e reinicia uma vez. Por isso, se o ecrã ficar
preto, reinicie (mantenha o botão de energia carregado se for preciso) e
deixe-o arrancar mais duas vezes.

O motivo fica registado em `/var/lib/gpushift/log` e no journal
(`journalctl -u gpushift-boot-check`). O reinício automático acontece no
máximo uma vez por troca, por isso nunca entra em ciclo.

Este passo precisa do systemd. Em sistemas sem systemd o serviço não é
instalado e só estão disponíveis os passos 2 a 5.

## 2. Consola de texto (TTY)

1. Carregue em **Ctrl+Alt+F3** (F2 a F6 também funcionam na maioria dos
   sistemas).
2. Inicie sessão com o seu utilizador e palavra-passe.
3. Execute:

   ```sh
   sudo gpushift reset
   sudo reboot
   ```

## 3. Parâmetro de arranque `gpushift.reset=1`

Não altera a configuração do gestor de arranque; o parâmetro vale só para esse
arranque. O GPUShift deteta-o cedo no arranque, remove todas as suas
alterações e reinicia uma vez com a configuração original.

**GRUB** (Debian, Ubuntu, Fedora, openSUSE, a maioria das instalações Arch):

1. Mostre o menu: mantenha **Shift** carregado durante o arranque (BIOS) ou
   carregue várias vezes em **Esc** (UEFI).
2. Selecione a entrada normal e carregue em **e**.
3. Procure a linha que começa por `linux` e acrescente ` gpushift.reset=1`
   no fim.
4. Arranque com **Ctrl+X** ou **F10**.

**systemd-boot**:

1. Mostre o menu: mantenha **Espaço** carregado durante o arranque.
2. Selecione a entrada normal e carregue em **e**.
3. Acrescente ` gpushift.reset=1` no fim da linha de opções.
4. Carregue em **Enter** para arrancar.

## 4. Arranque em modo de texto

Arranque sem a interface gráfica e reponha:

1. Edite a entrada de arranque como no passo 3, mas acrescente
   ` systemd.unit=multi-user.target`.
2. Inicie sessão na consola de texto e execute:

   ```sh
   sudo gpushift reset
   sudo reboot
   ```

## 5. Live USB

Arranque qualquer live USB de Linux e abra um terminal. Encontre a partição
raiz com `lsblk -f` e monte-a (substitua `/dev/nvme0n1p2` pela sua; em
instalações com Btrfs acrescente `-o subvol=@` ou o subvolume que a sua
distribuição usa):

```sh
sudo mount /dev/nvme0n1p2 /mnt
```

**Com o GPUShift no sistema live** (por exemplo, instalado a partir do .deb):

```sh
sudo gpushift reset --root /mnt
```

Remove os ficheiros do GPUShift de `/mnt`, repõe os ficheiros guardados em
backup e mostra o comando de initramfs da distribuição instalada.

**Sem o GPUShift**, faça o mesmo à mão:

```sh
sudo rm -f /mnt/etc/modprobe.d/gpushift.conf /mnt/etc/udev/rules.d/50-gpushift.rules
# Ficheiros que já existiam antes do GPUShift, se houver:
ls /mnt/var/lib/gpushift/backup/
sudo rm -rf /mnt/var/lib/gpushift
```

Em ambos os casos, regenere o initramfs num chroot. Monte primeiro `/boot`
(e `/boot/efi`) se forem partições separadas:

```sh
for d in dev proc sys run; do sudo mount --rbind /$d /mnt/$d; done
sudo chroot /mnt update-initramfs -u -k all        # Debian, Ubuntu
sudo chroot /mnt dracut --force --regenerate-all   # Fedora, openSUSE
sudo chroot /mnt mkinitcpio -P                     # Arch Linux
```

Depois reinicie sem a pen USB.

### Portáteis com MUX no firmware

Se usou o modo Dedicated num portátil ASUS, o MUX pode continuar a ligar o
ecrã à GPU dedicada. Quando o sistema arrancar, `sudo gpushift reset` repõe-no.
A partir de um live USB a correr no mesmo portátil também pode executar
`echo 1 | sudo tee /sys/devices/platform/asus-nb-wmi/gpu_mux_mode` e reiniciar.
