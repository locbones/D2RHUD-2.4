<?php
declare(strict_types=1);

trait MarketplaceListingComments {
    private function listingCommentsSchema(string $engine): void {
        $this->db->exec("CREATE TABLE IF NOT EXISTS market_listing_comments (listing CHAR(32) PRIMARY KEY, comment TEXT NOT NULL)$engine");
    }
    private static function listingComment(mixed $value): string {
        if(!is_string($value)||strlen($value)>960||!preg_match('/^.{0,240}$/usD',$value)||preg_match('/[\x00-\x08\x0b\x0c\x0e-\x1f\x7f]/',$value))throw new MarketError(422,'Write a listing comment of up to 240 characters.');
        return trim(preg_replace('/\s+/u',' ',$value));
    }
    private function setListingComment(string $listing,?string $comment): void {
        if($comment===null)return; // Older clients must not erase an existing comment.
        $comment=self::listingComment($comment);
        $this->run('DELETE FROM market_listing_comments WHERE listing=?',[$listing]);
        if($comment!=='')$this->run('INSERT INTO market_listing_comments VALUES (?,?)',[$listing,$comment]);
    }
}
