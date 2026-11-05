<?php
declare(strict_types=1);

// Bounded version-98 item reader/writer. Copy saved fields unchanged, mint a
// fresh native identity for the root AND every socket child, and recalculate
// stack-edit offsets when adding a UUID to a legacy item. No save file I/O.
final class GiftItemFactory {
    private int $at=0,$bits=0,$byte=0,$used=0,$nodes=0;
    private string $out='';private array $root=[];
    private function __construct(private string $input,private array $definition) {}
    private function write(int $value,int $n):void {
        for($i=0;$i<$n;$i++){$this->byte|=(($value>>$i)&1)<<$this->used;++$this->bits;if(++$this->used===8){$this->out.=chr($this->byte);$this->byte=$this->used=0;}}
    }
    private function read(int $n,bool $copy=true):int {
        if($n<0||$n>32||$this->at+$n>strlen($this->input)*8)throw new RuntimeException('Truncated gift template.');
        $v=0;for($i=0;$i<$n;$i++)$v|=((ord($this->input[intdiv($this->at+$i,8)])>>(($this->at+$i)%8))&1)<<$i;
        $this->at+=$n;if($copy)$this->write($v,$n);return $v;
    }
    private function skip(int $n):void {while($n>0){$take=min(32,$n);$this->read($take);$n-=$take;}}
    private function stat(int $id):array {
        $r=$this->definition['stats']['s'.$id]??null;
        if(!is_array($r)||count($r)!==4)throw new RuntimeException('Gift template uses an unsupported saved property.');return $r;
    }
    private function properties():void {
        for($i=0;$i<4096;$i++){$id=$this->read(9);if($id===511)return;
            $count=in_array($id,[17,48,50,52],true)?2:(in_array($id,[54,57],true)?3:1);
            for($j=0;$j<$count;$j++){$s=$this->stat($id+$j);$this->skip($s[0]+$s[1]);}
        }throw new RuntimeException('Gift property list exceeds its limit.');
    }
    private function gold():void {$this->skip($this->read(1)?32:12);$this->skip(1);}
    private function item(int $depth=0):void {
        if($depth>6||++$this->nodes>64||$this->at%8)throw new RuntimeException('Unsupported gift socket tree.');
        $flags=$this->read(32);$this->skip(3);$mode=$this->read(3);
        if($mode>6||$mode===3||$mode===5||($flags&(1<<16)))throw new RuntimeException('Only inventory items can be used as gift templates.');
        $this->skip(15);$codes=array_flip($this->definition['codes']);$code='';
        for($i=0;$i<4;$i++){$word='';for($n=0;$n<17;$n++){$word.=$this->read(1);if(isset($codes[$word])){$code.=chr($codes[$word]);break;}}if(strlen($code)!==$i+1)throw new RuntimeException('Invalid gift item code.');}
        $d=$this->definition['items'][$code]??null;if(!$d)throw new RuntimeException('Gift item is missing from this mod.');
        $compact=(bool)($flags&(1<<21));$children=0;$quality=2;$seed=0;
        if($compact){if($d[3])$this->gold();if($d[7])$this->skip($this->stat(356)[0]);}
        else{
            $children=$this->read(3);$this->read(32,false);$seed=random_int(1,0xffffffff);$this->write($seed,32);
            if(!$this->read(7))throw new RuntimeException('Invalid gift item level.');$quality=$this->read(4);if($quality<1||$quality>9)throw new RuntimeException('Invalid gift quality.');
            if($this->read(1))$this->skip(3);if($this->read(1))$this->skip(11);
            switch($quality){
                case 1:case 3:$this->skip(3);break;
                case 2:if($d[6])$this->skip(12);if($d[5])$this->skip(10);if($d[4])$this->skip(5);break;
                case 4:$this->skip(22);break;
                case 5:case 7:$this->skip(12);break;
                case 6:case 8:$this->skip(16);for($n=0;$n<3;$n++){if($this->read(1))$this->skip(11);if($this->read(1))$this->skip(11);}break;
                case 9:$this->skip(16);break;
            }
            if($flags&(1<<26))$this->skip(16);
            if($flags&(1<<24)){for($n=0;$n<32;$n++)if(!$this->read(8))break;if($n===32)throw new RuntimeException('Invalid personalized gift item.');}
        }
        $hadUuid=$this->read(1,false);$this->write(1,1);$uuid=random_bytes(16);
        for($i=0;$i<16;$i++){if($hadUuid)$this->read(8,false);$this->write(ord($uuid[$i]),8);}
        if(!$depth)$this->root=['uuid'=>bin2hex($uuid),'seed'=>$seed,'quantity'=>1,'quantity_bit'=>-1,'sockets'=>$children];
        if(!$compact){
            if($d[0])$this->skip($this->stat(31)[0]);
            if($d[0]||$d[1]){$s=$this->stat(73);if($this->read($s[0])-$s[2]>0)$this->skip($this->stat(72)[0]);}
            if($d[3])$this->gold();
            if($d[2]){$offset=$this->bits;$quantity=$this->read(9);if(!$quantity)throw new RuntimeException('Empty gift stack.');if(!$depth){$this->root['quantity']=$quantity;if($quality===2&&!$children)$this->root['quantity_bit']=$offset;}}
            if($flags&(1<<11))$this->skip($this->stat(194)[0]);elseif($children)throw new RuntimeException('Invalid gift socket count.');
            $extra=0;if($quality===5){$mask=$this->read(5);for($i=0;$i<5;$i++)$extra+=($mask>>$i)&1;}
            $this->properties();for($i=0;$i<$extra;$i++)$this->properties();if($flags&(1<<26))$this->properties();
        }
        if($this->at%8)$this->skip(8-$this->at%8);
        for($i=0;$i<$children;$i++)$this->item($depth+1);
    }
    public static function copy(string $hex,array $definition):array {
        if(strlen($hex)<14||strlen($hex)>65536||strlen($hex)%2||strspn($hex,'0123456789abcdef')!==strlen($hex)||($definition['schema']??null)!==2||($definition['reader']??null)!=='d2r-98-v1')throw new RuntimeException('A verified save catalog and complete item template are required.');
        $w=new self(hex2bin($hex),$definition);$w->item();
        if($w->at!==strlen($w->input)*8||$w->used||strlen($w->out)>32768)throw new RuntimeException('Gift item has an unsupported boundary.');
        return $w->root+['hex'=>bin2hex($w->out),'nodes'=>$w->nodes];
    }
}
