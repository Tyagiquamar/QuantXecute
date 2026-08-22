import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'QuantXExecute — Engineering Console',
  description:
    'Live engineering console for the QuantXExecute market-data and execution-simulation engine.',
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en">
      <body>{children}</body>
    </html>
  );
}
