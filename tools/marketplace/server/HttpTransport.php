<?php
declare(strict_types=1);

// The wire envelope avoids shared-host argument expansion of item properties.
// It is not authority: callers authenticate first, then validate decoded data
// through exactly the same command and custody handlers as legacy JSON.
final class MarketplaceHttpTransport {
    public static function wireLimit(int $limit): int { return intdiv($limit+2,3)*4+256; }
    public static function decode(string $raw,int $limit): array {
        if(strlen($raw)>self::wireLimit($limit))throw new MarketError(413,'Request too large.');
        $data=json_decode($raw,true,32,JSON_THROW_ON_ERROR);
        if(!is_array($data))throw new MarketError(422,'Invalid command.');
        if(array_key_exists('market_transport',$data)){
            if(count($data)!==2||($data['market_transport']??null)!=='json-base64-v1'||!is_string($data['payload']??null))throw new MarketError(422,'Unsupported marketplace transport.');
            $decoded=base64_decode($data['payload'],true);
            if($decoded===false||base64_encode($decoded)!==$data['payload'])throw new MarketError(422,'Invalid marketplace transport payload.');
            if(strlen($decoded)>$limit)throw new MarketError(413,'Request too large.');
            $data=json_decode($decoded,true,32,JSON_THROW_ON_ERROR);
            if(!is_array($data)||array_key_exists('market_transport',$data))throw new MarketError(422,'Invalid decoded command.');
        }elseif(strlen($raw)>$limit)throw new MarketError(413,'Request too large.');
        return $data;
    }
}
